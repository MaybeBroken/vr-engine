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

// --------
#include "CTX.h"
#include "OVR_Math.h"
// --------   doesn't interfere with anything; just for syntax help in this injector file

#app_init> w
virtual bool AppInit(const xrJava *context) override
{
    // APP_INIT_MOD_ENTRY
    auto fileSys = std::unique_ptr<OVRFW::ovrFileSys>(OVRFW::ovrFileSys::Create(*context));
    std::string heightmapPath = "apk:///assets/Gettysburg_heightmap.glb";
    if (fileSys)
    {
        static CTX::Model &heightmapModel = ctx_->LoadModel(*fileSys, heightmapPath);
        heightmapModel.setPos(0.0f, -0.2f, -0.4f);
        heightmapModel.setScale(0.2f);
        heightmapModel.setHpr(0.0f, 0.0f, 0.0f);

        // Demo gesture bindings: single-finger pinch moves; two-finger pinch scales+rotates+moves
        // Keep simple state for gesture processing
        // Gesture state
        static bool leftActive = false;
        static bool rightActive = false;
        static OVR::Vector3f leftPos(0.0f), rightPos(0.0f);

        // Baselines captured on gesture start
        static OVR::Vector3f oneFingerStartHand(0.0f);
        static OVR::Vector3f oneFingerModelStartPos(0.0f);

        static OVR::Vector3f twoFingerStartLeft(0.0f), twoFingerStartRight(0.0f);
        static OVR::Vector3f twoFingerStartCenter(0.0f);
        static float twoFingerStartDistance = 0.0f;
        static OVR::Vector3f twoFingerModelStartPos(0.0f);
        static float twoFingerModelStartScale = 0.2f;
        static float twoFingerModelStartHeadingDeg = 0.0f;

        auto applyTransform = [&]()
        {
            // One-finger: move relative to hand delta since gesture start
            if (leftActive ^ rightActive)
            {
                const OVR::Vector3f p = leftActive ? leftPos : rightPos;
                const OVR::Vector3f d = p - oneFingerStartHand;
                const float gain = 1.0f;
                OVR::Vector3f target = oneFingerModelStartPos + d * gain;
                heightmapModel.setPos(target.x, target.y, target.z);
            }
            // Two-finger: scale, rotate heading, and move to center between fingers
            else if (leftActive && rightActive)
            {
                const OVR::Vector3f center = (leftPos + rightPos) * 0.5f;
                const float dist = (leftPos - rightPos).Length();

                // Scale relative to start distance
                if (twoFingerStartDistance > 1e-5f)
                {
                    float scaleRatio = dist / twoFingerStartDistance;
                    float newScale = std::max(0.05f, twoFingerModelStartScale * scaleRatio);
                    heightmapModel.setScale(newScale);
                }

                // Heading delta relative to start vector
                OVR::Vector3f dcur = rightPos - leftPos;
                float headingRadCur = atan2f(dcur.x, dcur.z);
                OVR::Vector3f dstart = twoFingerStartRight - twoFingerStartLeft;
                float headingRadStart = atan2f(dstart.x, dstart.z);
                float headingDegDelta = (headingRadCur - headingRadStart) * (180.0f / MATH_FLOAT_PI);
                heightmapModel.setHpr(twoFingerModelStartHeadingDeg + headingDegDelta, 0.0f, 0.0f);

                // Move to center relative to start center
                const OVR::Vector3f centerDelta = center - twoFingerStartCenter;
                const OVR::Vector3f newPos = twoFingerModelStartPos + centerDelta;
                heightmapModel.setPos(newPos.x, newPos.y, newPos.z);
            }
            else
            {
                // No fingers active: nothing to apply
            }
        };

        // Bind pinch callbacks to update state and apply transforms
        ctx_->Bind(CTX::Action::PinchLeft, [&](const CTX::ActionEvent &e)
                   {
            const bool wasLeftActive = leftActive;
            leftActive = e.active;
            leftPos = e.position;

            // One-finger start (left-only)
            if (leftActive && !wasLeftActive && !rightActive)
            {
                oneFingerStartHand = leftPos;
                oneFingerModelStartPos = heightmapModel.getPos();
            }

            // Two-finger start when right already active
            if (leftActive && !wasLeftActive && rightActive)
            {
                twoFingerStartLeft = leftPos;
                twoFingerStartRight = rightPos;
                twoFingerStartCenter = (twoFingerStartLeft + twoFingerStartRight) * 0.5f;
                twoFingerStartDistance = (twoFingerStartLeft - twoFingerStartRight).Length();
                twoFingerModelStartPos = heightmapModel.getPos();
                twoFingerModelStartScale = heightmapModel.getScale().x;
                twoFingerModelStartHeadingDeg = heightmapModel.getHpr().x;
            }

            applyTransform(); });

        ctx_->Bind(CTX::Action::PinchRight, [&](const CTX::ActionEvent &e)
                   {
            const bool wasRightActive = rightActive;
            rightActive = e.active;
            rightPos = e.position;

            // One-finger start (right-only)
            if (rightActive && !wasRightActive && !leftActive)
            {
                oneFingerStartHand = rightPos;
                oneFingerModelStartPos = heightmapModel.getPos();
            }

            // Two-finger start when left already active
            if (rightActive && !wasRightActive && leftActive)
            {
                twoFingerStartLeft = leftPos;
                twoFingerStartRight = rightPos;
                twoFingerStartCenter = (twoFingerStartLeft + twoFingerStartRight) * 0.5f;
                twoFingerStartDistance = (twoFingerStartLeft - twoFingerStartRight).Length();
                twoFingerModelStartPos = heightmapModel.getPos();
                twoFingerModelStartScale = heightmapModel.getScale().x;
                twoFingerModelStartHeadingDeg = heightmapModel.getHpr().x;
            }

            applyTransform(); });

        ctx_->Bind(CTX::Action::ButtonA, [&](const CTX::ActionEvent &e)
                   {
            if (e.active && heightmapModel.HasAnimations())
            {
                heightmapModel.NextAnimation(true);
            } });

        ctx_->Bind(CTX::Action::ButtonB, [&](const CTX::ActionEvent &e)
                   {
            if (e.active && heightmapModel.HasAnimations())
            {
                heightmapModel.PrevAnimation(true);
            } });
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