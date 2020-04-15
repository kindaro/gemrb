## Plan:

1. Use local constant array of structs in a header as the single source of truth.
1. Use a named lambda for creating an appropriately terminated copy of that source.
1. Convert ugly strings to C++11 raw string literals.

## Structure:

1. `Functions.h` declares functions and global constant doc-strings.
1. `Functions.cpp` includes `Functions.h` and defines functions and doc-strings. Maybe structures with lambdas may be used instead.
1. `Table.h` includes `Functions.h` and defines the local constant array of structs. Beautiful because easily extensible and minimal. Every compilation unit will receive its own copy, but they are all identical.
1. The main source includes `Table.h` and converts the table for use via a named lambda. Beautiful because nearby and temporary.
    * Problem is, who is going to free the memory so allocated?
1. The doc-string extractor includes `Table.h` and extracts the doc-strings.

## Problems:

1. Why no one owns state such as `StoreSpellsCount`? It seems as though `GUIScript` should, seeing how they are used in its destructor. Or maybe `Interface`. There should be a _«state»_ structure.

    Complete list:

    * `ItemArray`
    * `SpellArray`
    * `StoreSpells`
    * `SpecialItems`
    * `UsedItems`
    * `StoreSpellsCount`
    * `SpecialItemsCount`
    * `UsedItemsCount`
    * `ReputationIncrease`
    * `ReputationDonation`
    * `GUIAction`
    * `GUITooltip`? — Not used in `GUIScript`, but seems to belong to the family.
    * `GUIResRef`? — Not used in `GUIScript`, but seems to belong to the family.
    * `GUIEvent`? — Not used in `GUIScript`, but seems to belong to the family.
    * `gametype_hint`
    * `gametype_hint_weight`
    
    Generally, top level `static` variables should be audited on this basis.
    
1. Then I may ask why `GuiScript` implies the initialized `Interface` _(as variable `core`)_ and whether it is a good thing.
1. `QuitOnError` is another suspicious variable. It seems like it should belong to some sort of a _«settings»_ structure for the Python interpreter.
1. Why does the constructor of `GUIScript` set `GUIAction[0]`? The arrays `GUIAction` and `GUITooltip` are initialized elsewhere. I have to move the declaration upper in scope to make it visible to the constructor.
1. Who owns method tables that are given to the Python API? I suppose I should have an internal variable for that.
1. To start from the beginning, why am I using a plain array? What if I were using a type that knows its length? Apparently there is no easy way to have it in C.
