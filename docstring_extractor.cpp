#include "gemrb/plugins/GUIScript/GUIScript.cpp"

int main (int, char**)
{
	chdir ("/tmp");

	char * free_me_last = strdup ("GemRB_GUIScript_docstrings_XXXXXX");
	const char * put_here = mkdtemp (free_me_last);

	if (put_here != NULL)
		{
			for (PyMethodDef * x = GemRBMethods; x->ml_name != NULL; x++)
				{
					char * locus = (char*) calloc (1, strlen (put_here) + 1 + strlen (x -> ml_name) + 1);
					strcpy(locus, put_here);
					strcat (locus, "/");
					strcat (locus, x -> ml_name);

					FILE * write_me = fopen (locus,"w");
					fprintf (write_me, "%s", x -> ml_doc);
					fclose (write_me);

					free (locus);
				}
			printf("/tmp/%s\n", put_here);
		}
	else
		{
			perror("Cannot create temporary directory.");
		}

	free (free_me_last);
}
