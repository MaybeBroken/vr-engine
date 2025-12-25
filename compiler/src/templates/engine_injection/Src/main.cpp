// To write an injection, you must write something like "#includes" or "#var_space", with an #end at the close of the section.
// To specify a mode (w for overwrite, a for append, p for prepend), write it like "#includes>w" or "#var_space>a".
// (The default mode is append (a)).

// The json below is a reference of all possible injection points, the first word is the name you use for it,
// what follows is the injection markers that can be found in the base engine file "file://./compiler/src/engine/VrEngine/Src/main.cpp".

//"includes": {"entry": INCLUDES_ENTRY, "exit": INCLUDES_EXIT},
// "var_space": {"entry": VAR_SPACE_ENTRY, "exit": VAR_SPACE_EXIT},
// "class": {"entry": CLASS_ENTRY, "exit": CLASS_EXIT},
// "entry_point_init": {"entry": ENTRY_POINT_INIT, "exit": ENTRY_POINT_EXIT},
// "public": {"entry": PUBLIC_ENTRY, "exit": PUBLIC_EXIT},
// "class_init": {"entry": CLASS_INIT_ENTRY, "exit": CLASS_INIT_EXIT},
// "app_extensions": {"entry": APP_EXTENSIONS_ENTRY, "exit": APP_EXTENSIONS_EXIT},
// "app_init": {"entry": APP_INIT_ENTRY, "exit": APP_INIT_EXIT},
// "app_shutdown": {"entry": APP_SHUTDOWN_ENTRY, "exit": APP_SHUTDOWN_EXIT},
// "session_init": {"entry": SESSION_INIT_ENTRY, "exit": SESSION_INIT_EXIT},
// "session_end": {"entry": SESSION_END_ENTRY, "exit": SESSION_END_EXIT},
// "update": {"entry": UPDATE_ENTRY, "exit": UPDATE_EXIT},
// "render": {"entry": RENDER_ENTRY, "exit": RENDER_EXIT},

// Example injection project:

// #INCLUDES> w
// #include <iostream>
// #end

// #VAR_SPACE> w
// // Example variable injection
// int exampleVariable = 42;
// #end

// #CLASS> w
// // Example class injection
// class VrEngine
// { // Note: class name must match the base engine class name, which is always VrEngine
// public:
//     void exampleMethod()
//     {
//         std::cout << "Hello from VrEngine! exampleVariable: " << exampleVariable << std::endl;
//     }
// };
// #end