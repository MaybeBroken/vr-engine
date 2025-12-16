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
#include "Misc/Log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <utility>
#include <string>
// --------   doesn't interfere with anything; just for syntax help in this injector file

#app_init> w
virtual bool AppInit(const xrJava *context) override
{
    // APP_INIT_MOD_ENTRY
    auto fileSys = std::unique_ptr<OVRFW::ovrFileSys>(OVRFW::ovrFileSys::Create(*context));
    // load model paths
    std::string baseModelPath = "apk:///assets/Gettysburg-";
    std::string heightmapPath = "apk:///assets/Gettysburg-0.glb";
    std::string handUIPath = "apk:///assets/GettysburgUI.glb";
    struct AnimationStep
    {
        int animIndex;
        CTX::Blocking blocking;
        bool loop;
        bool startOnSceneInit;
    };
    struct animatedFile
    {
        std::string path;
        std::vector<AnimationStep> steps;
        CTX::Model *model = nullptr;
    };

    static const std::vector<AnimationStep> defaultAnimationSteps = {
        // animIndex, blocking, loop, startOnSceneInit
        {0, CTX::Blocking::Local, false, true},
        {1, CTX::Blocking::Local, false, false},
        {2, CTX::Blocking::Local, false, false},
        {3, CTX::Blocking::Local, false, false},
        {4, CTX::Blocking::Local, false, false},
        {5, CTX::Blocking::Local, false, false},
        {6, CTX::Blocking::Local, false, false},
        {7, CTX::Blocking::Local, false, false},
        {8, CTX::Blocking::Local, false, false},
        {9, CTX::Blocking::Local, false, false},
        {10, CTX::Blocking::Local, false, false},
        {11, CTX::Blocking::Local, false, false},
        {12, CTX::Blocking::Local, false, false},
        {13, CTX::Blocking::Local, false, false},
        {14, CTX::Blocking::Local, false, false},
        {15, CTX::Blocking::Local, false, false}, // copy of 0, to loop back and reset scene
    };
    static std::vector<AnimationStep> animationSteps = defaultAnimationSteps;
    static std::vector<animatedFile> animatedFiles;
    if (animatedFiles.empty())
    {
        animatedFiles.reserve(35);
        for (int af = 1; af < 36; ++af)
        {
            animatedFile file{};
            file.path = baseModelPath + std::to_string(af) + ".glb";
            file.steps = defaultAnimationSteps;
            animatedFiles.push_back(std::move(file));
        }
    }

    static std::vector<CTX::Model *> sceneModels;

    static std::array<std::string, 16> uiPanels = {
        // panel names
        "1",
        "2",
        "3",
        "4",
        "5",
        "6",
        "7",
        "8",
        "9",
        "10",
        "11",
        "12",
        "13",
        "14",
        "15",
        "16",
    };
    static size_t currentAnimationStep = 0;
    lastRightPose = OVR::Vector3f(0.0f);
    if (fileSys)
    {
        const OVR::Vector3f initialPos(0.0f, -0.2f, -0.4f);
        const float initialScale = 0.2f;
        const float initialHeading = 0.0f;

        auto loadModelWithTransform = [&](const std::string &path, const OVR::Vector3f &pos, float scale, float headingDeg) -> CTX::Model *
        {
            CTX::Model &m = ctx_->LoadModel(*fileSys, path);
            if (!m.isLoaded())
            {
                ALOGE("CTX load failed for %s", path.c_str());
                // Keep the slot so we can diagnose counts; RenderAll will skip unloaded models.
                return nullptr;
            }
            m.setPos(pos.x, pos.y, pos.z);
            m.setScale(scale);
            m.setHpr(headingDeg, 0.0f, 0.0f);
            return &m;
        };

        CTX::Model *heightmapPtr = loadModelWithTransform(heightmapPath, initialPos, initialScale, initialHeading);
        if (!heightmapPtr)
        {
            return false; // critical scene asset failed to load
        }
        CTX::Model &heightmapModel = *heightmapPtr;

        sceneModels.clear();
        sceneModels.reserve(animatedFiles.size() + 1);
        sceneModels.push_back(&heightmapModel);

        for (auto &af : animatedFiles)
        {
            if (af.model == nullptr)
            {
                af.model = loadModelWithTransform(af.path, initialPos, initialScale, initialHeading);
            }
            if (af.model && af.model->isLoaded())
            {
                sceneModels.push_back(af.model);
            }
        }

        CTX::Model *handUIPtr = loadModelWithTransform(handUIPath, OVR::Vector3f(0.0f, 0.0f, -0.2f), 0.3f, 0.0f);
        if (!handUIPtr)
        {
            return false; // UI missing; bail early so we notice
        }
        static CTX::Model &handUIModel = *handUIPtr;
        // hide all panels initially
        for (const auto &panel : uiPanels)
        {
            handUIModel.setActiveNode(panel, false);
        }
        handUIModel.setActiveNode("0", true); // ensure default panel is shown
        handUIRef = &handUIModel;

        auto primaryModel = [&]() -> CTX::Model *
        {
            return sceneModels.empty() ? nullptr : sceneModels.front();
        };

        auto forEachModel = [&](auto &&fn)
        {
            for (auto *m : sceneModels)
            {
                if (m)
                {
                    fn(*m);
                }
            }
        };

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
            auto *modelRef = primaryModel();
            if (!modelRef)
            {
                return;
            }

            // One-finger: move relative to hand delta since gesture start
            if (leftActive ^ rightActive)
            {
                const OVR::Vector3f p = leftActive ? leftPos : rightPos;
                const OVR::Vector3f d = p - oneFingerStartHand;
                const float gain = 1.0f;
                OVR::Vector3f target = oneFingerModelStartPos + d * gain;
                forEachModel([&](CTX::Model &m)
                             { m.setPos(target.x, target.y, target.z); });
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
                    forEachModel([&](CTX::Model &m)
                                 { m.setScale(newScale); });
                }

                // Heading delta relative to start vector
                OVR::Vector3f dcur = rightPos - leftPos;
                float headingRadCur = atan2f(dcur.x, dcur.z);
                OVR::Vector3f dstart = twoFingerStartRight - twoFingerStartLeft;
                float headingRadStart = atan2f(dstart.x, dstart.z);
                float headingDegDelta = (headingRadCur - headingRadStart) * (180.0f / MATH_FLOAT_PI);
                forEachModel([&](CTX::Model &m)
                             { m.setHpr(twoFingerModelStartHeadingDeg + headingDegDelta, 0.0f, 0.0f); });

                // Move to center relative to start center
                const OVR::Vector3f centerDelta = center - twoFingerStartCenter;
                const OVR::Vector3f newPos = twoFingerModelStartPos + centerDelta;
                forEachModel([&](CTX::Model &m)
                             { m.setPos(newPos.x, newPos.y, newPos.z); });
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
                if (auto *modelRef = primaryModel())
                {
                    oneFingerModelStartPos = modelRef->getPos();
                }
            }

            // Two-finger start when right already active
            if (leftActive && !wasLeftActive && rightActive)
            {
                twoFingerStartLeft = leftPos;
                twoFingerStartRight = rightPos;
                twoFingerStartCenter = (twoFingerStartLeft + twoFingerStartRight) * 0.5f;
                twoFingerStartDistance = (twoFingerStartLeft - twoFingerStartRight).Length();
                if (auto *modelRef = primaryModel())
                {
                    twoFingerModelStartPos = modelRef->getPos();
                    twoFingerModelStartScale = modelRef->getScale().x;
                    twoFingerModelStartHeadingDeg = modelRef->getHpr().x;
                }
            }

            applyTransform(); });

        ctx_->Bind(CTX::Action::PinchRight, [&](const CTX::ActionEvent &e)
                   {
            const bool wasRightActive = rightActive;
            rightActive = e.active;
            rightPos = e.position;
            lastRightPose = e.position;

            // Keep the hand UI anchored to the right controller position.
            handUIModel.setPos(rightPos.x, rightPos.y, rightPos.z);

            // One-finger start (right-only)
            if (rightActive && !wasRightActive && !leftActive)
            {
                oneFingerStartHand = rightPos;
                if (auto *modelRef = primaryModel())
                {
                    oneFingerModelStartPos = modelRef->getPos();
                }
            }

            // Two-finger start when left already active
            if (rightActive && !wasRightActive && leftActive)
            {
                twoFingerStartLeft = leftPos;
                twoFingerStartRight = rightPos;
                twoFingerStartCenter = (twoFingerStartLeft + twoFingerStartRight) * 0.5f;
                twoFingerStartDistance = (twoFingerStartLeft - twoFingerStartRight).Length();
                if (auto *modelRef = primaryModel())
                {
                    twoFingerModelStartPos = modelRef->getPos();
                    twoFingerModelStartScale = modelRef->getScale().x;
                    twoFingerModelStartHeadingDeg = modelRef->getHpr().x;
                }
            }

            applyTransform(); });

        auto showCurrentPanel = [&]()
        {
            // Hide all panels
            for (const auto &panel : uiPanels)
            {
                handUIModel.setActiveNode(panel, false);
            }
            // Show current panel if valid
            if (currentAnimationStep < uiPanels.size())
            {
                handUIModel.setActiveNode(uiPanels[currentAnimationStep], true);
            }
        };

        auto playStepOnModel = [&](CTX::Model &model, size_t stepIndex, bool forward) -> bool
        {
            if (animationSteps.empty() || stepIndex >= animationSteps.size())
            {
                return false;
            }
            const auto &step = animationSteps[stepIndex];
            const auto animationCount = static_cast<size_t>(std::max(0, model.GetAnimationCount()));
            const bool hasAnimations = model.HasAnimations();
            if (hasAnimations && step.animIndex >= 0 && static_cast<size_t>(step.animIndex) < animationCount)
            {
                return model.PlayAnimationByIndex(
                    step.animIndex,
                    step.loop ? OVRFW::MODEL_ANIMATION_TIME_TYPE_LOOP_FORWARD : OVRFW::MODEL_ANIMATION_TIME_TYPE_ONCE_FORWARD,
                    1.0f,
                    0.0f,
                    step.loop,
                    step.blocking);
            }

            if (!hasAnimations)
            {
                return false;
            }
            return forward ? model.NextAnimation(step.loop, step.blocking)
                           : model.PrevAnimation(step.loop, step.blocking);
        };

        auto playStepAll = [&](size_t stepIndex, bool forward) -> bool
        {
            bool anyPlayed = false;
            forEachModel([&](CTX::Model &model)
                         { anyPlayed = playStepOnModel(model, stepIndex, forward) || anyPlayed; });
            return anyPlayed;
        };

        auto anyModelHasAnimations = [&]() -> bool
        {
            return std::any_of(sceneModels.begin(), sceneModels.end(), [](CTX::Model *m)
                               { return m && m->HasAnimations(); });
        };

        ctx_->Bind(CTX::Action::ButtonA, [&](const CTX::ActionEvent &e)
                   {
            if (!e.active)
            {
                return;
            }
            const size_t nextStep = animationSteps.empty() ? currentAnimationStep : (currentAnimationStep + 1) % animationSteps.size();
            const bool played = playStepAll(nextStep, true);
            if (played)
            {
                currentAnimationStep = nextStep;
                showCurrentPanel();
            }
            else if (anyModelHasAnimations())
            {
                // Last-resort fallback: advance animation ring if custom step failed.
                forEachModel([&](CTX::Model &model)
                             { model.NextAnimation(false, CTX::Blocking::None); });
                currentAnimationStep = nextStep;
                showCurrentPanel();
            } });

        ctx_->Bind(CTX::Action::ButtonB, [&](const CTX::ActionEvent &e)
                   {
            if (!e.active)
            {
                return;
            }

            size_t prevStep = 0;
            if (!animationSteps.empty())
            {
                prevStep = (currentAnimationStep == 0) ? animationSteps.size() - 1
                                                      : (currentAnimationStep - 1) % animationSteps.size();
            }
            const bool played = playStepAll(prevStep, false);
            if (played)
            {
                currentAnimationStep = prevStep;
                showCurrentPanel();
            }
            else if (anyModelHasAnimations())
            {
                forEachModel([&](CTX::Model &model)
                             { model.PrevAnimation(false, CTX::Blocking::None); });
                currentAnimationStep = prevStep;
                showCurrentPanel();
            } });

        // Kick off any step flagged for auto-start, choosing the first valid one.
        auto itStart = std::find_if(animationSteps.begin(), animationSteps.end(), [](const AnimationStep &s)
                                    { return s.startOnSceneInit; });
        if (itStart != animationSteps.end())
        {
            const size_t idx = static_cast<size_t>(std::distance(animationSteps.begin(), itStart));
            const auto &step = *itStart;
            bool started = false;
            forEachModel([&](CTX::Model &model)
                         {
                             const auto animationCount = static_cast<size_t>(std::max(0, model.GetAnimationCount()));
                             if (step.animIndex >= 0 && static_cast<size_t>(step.animIndex) < animationCount)
                             {
                                 started = model.PlayAnimationByIndex(
                                    step.animIndex,
                                    step.loop ? OVRFW::MODEL_ANIMATION_TIME_TYPE_LOOP_FORWARD : OVRFW::MODEL_ANIMATION_TIME_TYPE_ONCE_FORWARD,
                                    1.0f,
                                    0.0f,
                                    step.loop,
                                    step.blocking) || started;
                             } });
            if (started)
            {
                currentAnimationStep = idx;
                showCurrentPanel();
            }
        }
    }
    // APP_INIT_MOD_EXIT
    return true;
}
#end

#update> p
// Drive hand UI to follow the right controller/hand every frame, not only on pinch callbacks.
if (handUIRef)
{
    // If the runtime reports the right remote is tracked, use that pose directly.
    if (in.RightRemoteTracked)
    {
        const OVR::Posef pose = in.RightRemotePose;
        handUIRef->setPos(pose.Translation.x, pose.Translation.y, pose.Translation.z);
        // Optionally align orientation to controller heading only (ignore roll/pitch to avoid tilt).
        handUIRef->setQuat(pose.Rotation);
        lastRightPose = pose.Translation;
    }
    else
    {
        // Fallback to the last hand-tracking-reported position so UI stays near the hand.
        handUIRef->setPos(lastRightPose.x, lastRightPose.y, lastRightPose.z);
    }
}

#end

#private> p
OVR::Vector3f lastRightPose;
CTX::Model *handUIRef = nullptr;
#end