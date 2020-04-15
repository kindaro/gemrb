/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 */

#include "FunctionTables.h"

#include "GUIScript.h"

#include "Audio.h"
#include "CharAnimations.h"
#include "ControlAnimation.h"
#include "DataFileMgr.h"
#include "DialogHandler.h"
#include "DisplayMessage.h"
#include "EffectQueue.h"
#include "Game.h"
#include "GameData.h"
#include "ImageFactory.h"
#include "ImageMgr.h"
#include "Interface.h"
#include "Item.h"
#include "Map.h"
#include "MusicMgr.h"
#include "Palette.h"
#include "PalettedImageMgr.h"
#include "ResourceDesc.h"
#include "SaveGameIterator.h"
#include "Spell.h"
#include "TableMgr.h"
#include "TileMap.h"
#include "Video.h"
#include "WorldMap.h"
#include "GameScript/GSUtils.h" //checkvariable
#include "GUI/Button.h"
#include "GUI/EventMgr.h"
#include "GUI/GameControl.h"
#include "GUI/Label.h"
#include "GUI/MapControl.h"
#include "GUI/TextArea.h"
#include "GUI/TextEdit.h"
#include "GUI/Window.h"
#include "GUI/WorldMapControl.h"
#include "Scriptable/Container.h"
#include "Scriptable/Door.h"
#include "Scriptable/InfoPoint.h"
#include "System/FileStream.h"
#include "System/Logger/MessageWindowLogger.h"
#include "System/VFS.h"

using namespace GemRB;

GUIScript *GemRB::gs = NULL;

GUIScript::GUIScript(void)
{
	gs = this;
	pDict = NULL; //borrowed, but used outside a function
	pModule = NULL; //should decref it
	pMainDic = NULL; //borrowed, but used outside a function
	pGUIClasses = NULL;
}

GUIScript::~GUIScript(void)
{
	if (Py_IsInitialized()) {
		if (pModule) {
			Py_DECREF( pModule );
		}
		Py_Finalize();
	}
	if (ItemArray) {
		free(ItemArray);
		ItemArray=NULL;
	}
	if (SpellArray) {
		free(SpellArray);
		SpellArray=NULL;
	}
	if (StoreSpells) {
		free(StoreSpells);
		StoreSpells=NULL;
	}
	if (SpecialItems) {
		free(SpecialItems);
		SpecialItems=NULL;
	}
	if (UsedItems) {
		free(UsedItems);
		UsedItems=NULL;
	}

	StoreSpellsCount = -1;
	SpecialItemsCount = -1;
	UsedItemsCount = -1;
	ReputationIncrease[0]=(int) UNINIT_IEDWORD;
	ReputationDonation[0]=(int) UNINIT_IEDWORD;
	GUIAction[0]=UNINIT_IEDWORD;
}

/**
 * Quote path for use in python strings.
 * On windows also convert backslashes to forward slashes.
 */
static char* QuotePath(char* tgt, const char* src)
{
	char *p = tgt;
	char c;

	do {
		c = *src++;
#ifdef WIN32
		if (c == '\\') c = '/';
#endif
		if (c == '"' || c == '\\') *p++ = '\\';
	} while (0 != (*p++ = c));
	return tgt;
}


PyDoc_STRVAR( GemRB__doc,
"Module exposing GemRB data and engine internals\n\n"
"This module exposes to python GUIScripts GemRB engine data and internals. "
"It's implemented in gemrb/plugins/GUIScript/GUIScript.cpp" );

PyDoc_STRVAR( GemRB_internal__doc,
"Internal module for GemRB metaclasses.\n\n"
"This module is only for implementing GUIClass.py."
"It's implemented in gemrb/plugins/GUIScript/GUIScript.cpp" );

/** Initialization Routine */

bool GUIScript::Init(void)
{
	Py_Initialize();
	if (!Py_IsInitialized()) {
		return false;
	}

	PyObject *pMainMod = PyImport_AddModule( "__main__" );
	/* pMainMod is a borrowed reference */
	pMainDic = PyModule_GetDict( pMainMod );
	/* pMainDic is a borrowed reference */

  /* Python API requires a pointer to an appropriately terminated list, so we
  allocate such list on the heap and populate it from our morally immutable
  array. */
  PyMethodDef * (* allocate_method_list) (PyMethodDef const * const, int) = [ ]
    (PyMethodDef const * const u, int n)
  {
    PyMethodDef * v = (PyMethodDef *) calloc ((n + 1), sizeof (PyMethodDef));
    memcpy (v, u, n * sizeof (PyMethodDef));
    return v;
  };

  PyObject* pGemRB = Py_InitModule3( "GemRB", allocate_method_list (GemRBMethods, sizeof GemRBMethods / sizeof (PyMethodDef)), GemRB__doc );
  if (!pGemRB) {
		return false;
	}

	PyObject* p_GemRB = Py_InitModule3( "_GemRB", allocate_method_list (GemRBInternalMethods, sizeof GemRBInternalMethods / sizeof (PyMethodDef)), GemRB_internal__doc );
	if (!p_GemRB) {
		return false;
	}

	char string[_MAX_PATH+200];

	sprintf( string, "import sys" );
	if (PyRun_SimpleString( string ) == -1) {
		Log(ERROR, "GUIScript", "Error running: %s", string);
		return false;
	}

	// 2.6+ only, so we ignore failures
	sprintf( string, "sys.dont_write_bytecode = True");
	PyRun_SimpleString( string );

	char path[_MAX_PATH];
	char path2[_MAX_PATH];
	char quoted[_MAX_PATH];

	PathJoin(path, core->GUIScriptsPath, "GUIScripts", NULL);

	// Add generic script path early, so GameType detection works
	sprintf( string, "sys.path.append(\"%s\")", QuotePath( quoted, path ));
	if (PyRun_SimpleString( string ) == -1) {
		Log(ERROR, "GUIScript", "Error running: %s", string);
		return false;
	}

	sprintf( string, "import GemRB\n");
	if (PyRun_SimpleString( "import GemRB" ) == -1) {
		Log(ERROR, "GUIScript", "Error running: %s", string);
		return false;
	}

	sprintf(string, "GemRB.Version = '%s'", VERSION_GEMRB);
	PyRun_SimpleString(string);

	// Detect GameType if it was set to auto
	if (stricmp( core->GameType, "auto" ) == 0) {
		Autodetect();
	}

	// use the iwd guiscripts for how, but leave its override
	if (stricmp( core->GameType, "how" ) == 0) {
		PathJoin(path2, path, "iwd", NULL);
	} else {
		PathJoin(path2, path, core->GameType, NULL);
	}

	// GameType-specific import path must have a higher priority than
	// the generic one, so insert it before it
	sprintf( string, "sys.path.insert(-1, \"%s\")", QuotePath( quoted, path2 ));
	if (PyRun_SimpleString( string ) == -1) {
		Log(ERROR, "GUIScript", "Error running: %s", string );
		return false;
	}
	sprintf( string, "GemRB.GameType = \"%s\"", core->GameType);
	if (PyRun_SimpleString( string ) == -1) {
		Log(ERROR, "GUIScript", "Error running: %s", string );
		return false;
	}

#if PY_MAJOR_VERSION == 2
#if PY_MINOR_VERSION > 5
	// warn about python stuff that will go away in 3.0
	Py_Py3kWarningFlag = true;
#endif
#endif

	if (PyRun_SimpleString( "from GUIDefines import *" ) == -1) {
		Log(ERROR, "GUIScript", "Check if %s/GUIDefines.py exists!", path);
		return false;
	}

	if (PyRun_SimpleString( "from GUIClasses import *" ) == -1) {
		Log(ERROR, "GUIScript", "Check if %s/GUIClasses.py exists!", path);
		return false;
	}

	if (PyRun_SimpleString( "from GemRB import *" ) == -1) {
		Log(ERROR, "GUIScript", "builtin GemRB module failed to load!!!");
		return false;
	}

	// TODO: Put this file somewhere user editable
	// TODO: Search multiple places for this file
	char include[_MAX_PATH];
	PathJoin(include, core->GUIScriptsPath, "GUIScripts/include.py", NULL);
	ExecFile(include);

	PyObject *pClassesMod = PyImport_AddModule( "GUIClasses" );
	/* pClassesMod is a borrowed reference */
	pGUIClasses = PyModule_GetDict( pClassesMod );
	/* pGUIClasses is a borrowed reference */

	return true;
}

bool GUIScript::Autodetect(void)
{
	Log(MESSAGE, "GUIScript", "Detecting GameType.");

	char path[_MAX_PATH];
	PathJoin( path, core->GUIScriptsPath, "GUIScripts", NULL );
	DirectoryIterator iter( path );
	if (!iter)
		return false;

	gametype_hint[0] = '\0';
	gametype_hint_weight = 0;

	do {
		const char *dirent = iter.GetName();
		char module[_MAX_PATH];

		//print("DE: %s", dirent);
		if (iter.IsDirectory() && dirent[0] != '.') {
			// NOTE: these methods subtly differ in sys.path content, need for __init__.py files ...
			// Method1:
			PathJoin(module, core->GUIScriptsPath, "GUIScripts", dirent, "Autodetect.py", NULL);
			ExecFile(module);
			// Method2:
			//strcpy( module, dirent );
			//strcat( module, ".Autodetect");
			//LoadScript(module);
		}
	} while (++iter);

	if (gametype_hint[0]) {
		Log(MESSAGE, "GUIScript", "Detected GameType: %s", gametype_hint);
		strcpy(core->GameType, gametype_hint);
		return true;
	}
	else {
		Log(ERROR, "GUIScript", "Failed to detect game type.");
		return false;
	}
}

bool GUIScript::LoadScript(const char* filename)
{
	if (!Py_IsInitialized()) {
		return false;
	}
	Log(MESSAGE, "GUIScript", "Loading Script %s.", filename);

	PyObject *pName = PyString_FromString( filename );
	/* Error checking of pName left out */
	if (pName == NULL) {
		Log(ERROR, "GUIScript", "Failed to create filename for script \"%s\".", filename);
		return false;
	}

	if (pModule) {
		Py_DECREF( pModule );
	}

	pModule = PyImport_Import( pName );
	Py_DECREF( pName );

	if (pModule != NULL) {
		pDict = PyModule_GetDict( pModule );
		if (PyDict_Merge( pDict, pMainDic, false ) == -1)
			return false;
		/* pDict is a borrowed reference */
	} else {
		PyErr_Print();
		Log(ERROR, "GUIScript", "Failed to load script \"%s\".", filename);
		return false;
	}
	return true;
}

/* Similar to RunFunction, but with parameters, and doesn't necessarily fail */
PyObject *GUIScript::RunFunction(const char* moduleName, const char* functionName, PyObject* pArgs, bool report_error)
{
	if (!Py_IsInitialized()) {
		return NULL;
	}

	PyObject *module;
	if (moduleName) {
		module = PyImport_ImportModule(const_cast<char*>(moduleName));
	} else {
		module = pModule;
		Py_XINCREF(module);
	}
	if (module == NULL) {
		PyErr_Print();
		return NULL;
	}
	PyObject *dict = PyModule_GetDict(module);

	PyObject *pFunc = PyDict_GetItemString(dict, const_cast<char*>(functionName));
	/* pFunc: Borrowed reference */
	if (!pFunc || !PyCallable_Check(pFunc)) {
		if (report_error) {
			Log(ERROR, "GUIScript", "Missing function: %s from %s", functionName, moduleName);
		}
		Py_DECREF(module);
		return NULL;
	}
	PyObject *pValue = PyObject_CallObject( pFunc, pArgs );
	if (pValue == NULL) {
		if (PyErr_Occurred()) {
			PyErr_Print();
		}
	}
	Py_DECREF(module);
	return pValue;
}

bool GUIScript::RunFunction(const char *moduleName, const char* functionName, bool report_error, int intparam)
{
	PyObject *pArgs;
	if (intparam == -1) {
		pArgs = NULL;
	} else {
		pArgs = Py_BuildValue("(i)", intparam);
	}
	PyObject *pValue = RunFunction(moduleName, functionName, pArgs, report_error);
	Py_XDECREF(pArgs);
	if (pValue == NULL) {
		if (PyErr_Occurred()) {
			PyErr_Print();
		}
		return false;
	}
	Py_DECREF(pValue);
	return true;
}

bool GUIScript::RunFunction(const char *moduleName, const char* functionName, bool report_error, Point param)
{
	PyObject *pArgs = Py_BuildValue("(ii)", param.x, param.y);
	PyObject *pValue = RunFunction(moduleName, functionName, pArgs, report_error);
	Py_XDECREF(pArgs);
	if (pValue == NULL) {
		if (PyErr_Occurred()) {
			PyErr_Print();
		}
		return false;
	}
	Py_DECREF(pValue);
	return true;
}

void GUIScript::ExecFile(const char* file)
{
	FileStream fs;
	if (!fs.Open(file))
		return;

	int len = fs.Remains();
	if (len <= 0)
		return;

	char* buffer = (char *) malloc(len+1);
	if (!buffer)
		return;

	if (fs.Read(buffer, len) == GEM_ERROR) {
		free(buffer);
		return;
	}
	buffer[len] = 0;

	ExecString(buffer);
	free(buffer);
}

/** Exec a single String */
void GUIScript::ExecString(const char* string, bool feedback)
{
	PyObject* run = PyRun_String(string, Py_file_input, pMainDic, pMainDic);

	if (run) {
		// success
		if (feedback) {
			PyObject* pyGUI = PyImport_ImportModule("GUICommon");
			if (pyGUI) {
				PyObject* catcher = PyObject_GetAttrString(pyGUI, "outputFunnel");
				if (catcher) {
					PyObject* output = PyObject_GetAttrString(catcher, "lastLine");
					String* msg = StringFromCString(PyString_AsString(output));
					displaymsg->DisplayString(*msg, DMC_WHITE, NULL);
					delete msg;
					Py_DECREF(catcher);
				}
				Py_DECREF(pyGUI);
			}
		}
		Py_DECREF(run);
	} else {
		// failure
		PyObject *ptype, *pvalue, *ptraceback;
		PyErr_Fetch(&ptype, &pvalue, &ptraceback);

		//Get error message
		String* error = StringFromCString(PyString_AsString(pvalue));
		if (error) {
			displaymsg->DisplayString(L"Error: " + *error, DMC_RED, NULL);
		}
		PyErr_Print();
		Py_DECREF(ptype);
		Py_DECREF(pvalue);
		if (ptraceback) Py_DECREF(ptraceback);
		delete error;
	}
	PyErr_Clear();
}

PyObject* GUIScript::ConstructObject(const char* type, int arg)
{
	PyObject* tuple = PyTuple_New(1);
	PyTuple_SET_ITEM(tuple, 0, PyInt_FromLong(arg));
	PyObject* ret = gs->ConstructObject(type, tuple);
	Py_DECREF(tuple);
	return ret;
}

PyObject* GUIScript::ConstructObject(const char* type, PyObject* pArgs)
{
	char classname[_MAX_PATH] = "G";
	strncat(classname, type, _MAX_PATH - 2);
	if (!pGUIClasses) {
		char buf[256];
		snprintf(buf, sizeof(buf), "Tried to use an object (%.50s) before script compiled!", classname);
		return RuntimeError(buf);
	}

	PyObject* cobj = PyDict_GetItemString( pGUIClasses, classname );
	if (!cobj) {
		char buf[256];
		snprintf(buf, sizeof(buf), "Failed to lookup name '%.50s'", classname);
		return RuntimeError(buf);
	}
	PyObject* ret = PyObject_Call(cobj, pArgs, NULL);
	if (!ret) {
		return RuntimeError("Failed to call constructor");
	}
	return ret;
}

#include "plugindef.h"

GEMRB_PLUGIN(0x1B01BE6B, "GUI Script Engine (Python)")
PLUGIN_CLASS(IE_GUI_SCRIPT_CLASS_ID, GUIScript)
END_PLUGIN()

