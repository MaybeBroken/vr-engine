// To write an injection, you must write something like "#includes" or "#var_space", with an #end at the close of the section.
// To specify a mode (w for overwrite, a for append, p for prepend), write it like "#includes>w" or "#var_space>a".
// (The default mode is append (a)).

// The json below is a reference of all possible injection points, the first word is the name you use for it,
// what follows is the injection markers that can be found in the base engine file "XrSamples/VrEngine/Src/main.cpp"

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

#app_init> w
virtual bool AppInit(const xrJava *context) override
{
    // APP_INIT_MOD_ENTRY
    auto fileSys = std::unique_ptr<OVRFW::ovrFileSys>(OVRFW::ovrFileSys::Create(*context));

    std::string heightmapPath = "apk:///assets/Gettysburg_heightmap.glb";
    if (fileSys)
    {
        CTX::Model &heightmapModel = ctx_->LoadModel(*fileSys, heightmapPath);
        heightmapModel.setPos(0.0f, 1.0f, 0.0f);
        heightmapModel.setScale(1.0f);
        heightmapModel.setHpr(0.0f, 0.0f, 0.0f);
    }

    // APP_INIT_MOD_EXIT
    return true;
}
#end