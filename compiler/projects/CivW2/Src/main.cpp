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
        heightmapModel.setPos(0.0f, 0.2f, 0.4f);
        heightmapModel.setScale(0.2f);
        heightmapModel.setHpr(0.0f, 0.0f, 0.0f);

        // Demo gesture bindings: single-finger pinch moves; two-finger pinch scales+rotates+moves
        // Keep simple state for gesture processing
        static bool leftActive = false;
        static bool rightActive = false;
        static OVR::Vector3f leftPos(0.0f), rightPos(0.0f);
        static OVR::Vector3f lastCenter(0.0f);
        static float lastDistance = 0.0f;
        static float baseScale = 0.2f; // initial
        static float baseHeadingDeg = 0.0f;

        auto applyTransform = [&]()
        {
            // One-finger: move by left or right position deltas
            if (leftActive ^ rightActive)
            {
                const OVR::Vector3f p = leftActive ? leftPos : rightPos;
                // Map hand space movement directly to model position with small gain
                OVR::Vector3f target = p;
                const float gain = 0.5f;
                heightmapModel.setPos(target.x * gain, target.y * gain + 0.2f, target.z * gain + 0.4f);
            }
            // Two-finger: scale, rotate heading, and move to center between fingers
            else if (leftActive && rightActive)
            {
                OVR::Vector3f center = (leftPos + rightPos) * 0.5f;
                float dist = (leftPos - rightPos).Length();
                if (lastDistance > 0.0f)
                {
                    float scaleDelta = (dist - lastDistance);
                    float newScale = std::max(0.05f, baseScale + scaleDelta);
                    heightmapModel.setScale(newScale);
                }
                // Rotate around H axis based on horizontal vector between hands
                OVR::Vector3f d = rightPos - leftPos;
                float headingRad = atan2f(d.x, d.z);
                float headingDeg = headingRad * (180.0f / MATH_FLOAT_PI);
                heightmapModel.setHpr(baseHeadingDeg + headingDeg, 0.0f, 0.0f);

                // Move to center with gain
                const float moveGain = 0.5f;
                heightmapModel.setPos(center.x * moveGain, center.y * moveGain + 0.2f, center.z * moveGain + 0.4f);

                lastCenter = center;
                lastDistance = dist;
            }
        };

        // Bind pinch callbacks to update state and apply transforms
        ctx_->Bind(CTX::Action::PinchLeft, [&](const CTX::ActionEvent &e)
                   {
            leftActive = e.active;
            leftPos = e.position;
            if (e.active && !(rightActive))
            {
                // reset two-finger baseline when transitioning to two-finger
                lastDistance = 0.0f;
            }
            applyTransform(); });

        ctx_->Bind(CTX::Action::PinchRight, [&](const CTX::ActionEvent &e)
                   {
            rightActive = e.active;
            rightPos = e.position;
            if (e.active && !(leftActive))
            {
                lastDistance = 0.0f;
            }
            applyTransform(); });
    }
    // APP_INIT_MOD_EXIT
    return true;
}
#end

// #update> a
// virtual void Update(const OVRFW::ovrApplFrameIn &in) override
// {
//     // No-op: transforms are applied inside bound callbacks in AppInit.
// }
// #end