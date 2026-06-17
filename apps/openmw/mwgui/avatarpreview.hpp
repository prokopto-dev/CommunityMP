#ifndef MWGUI_AVATAR_PREVIEW_H
#define MWGUI_AVATAR_PREVIEW_H

#include <algorithm>
#include <cmath>

#include <MyGUI_Delegate.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_MouseButton.h>
#include <MyGUI_Widget.h>

#include "../mwrender/characterpreview.hpp"

namespace MWGui
{
    class AvatarPreviewController
    {
    public:
        void bind(MyGUI::ImageBox* image, MWRender::RaceSelectionPreview* preview)
        {
            mImage = image;
            mPreview = preview;

            if (mPreview == nullptr)
            {
                mRotating = false;
                mFocusing = false;
                mAngularVelocity = 0.f;
            }

            if (mImage == nullptr)
                return;

            if (mBoundImage != mImage)
            {
                mImage->eventMouseButtonPressed
                    += MyGUI::newDelegate(this, &AvatarPreviewController::onMouseButtonPressed);
                mImage->eventMouseButtonReleased
                    += MyGUI::newDelegate(this, &AvatarPreviewController::onMouseButtonReleased);
                mImage->eventMouseDrag += MyGUI::newDelegate(this, &AvatarPreviewController::onMouseDrag);
                mImage->eventMouseWheel += MyGUI::newDelegate(this, &AvatarPreviewController::onMouseWheel);
                mImage->eventMouseButtonDoubleClick
                    += MyGUI::newDelegate(this, &AvatarPreviewController::onMouseDoubleClick);
                mBoundImage = mImage;
            }

            setZoom(mZoom);
            setVerticalFocus(mVerticalFocus);
        }

        void setAngle(float angle)
        {
            mAngle = std::fmod(angle, twoPi);
            if (mAngle < 0.f)
                mAngle += twoPi;

            if (mPreview != nullptr)
                mPreview->setAngle(mAngle);
        }

        float getAngle() const { return mAngle; }

        void configureInspectionLimits(float maxZoom, float maxVerticalFocus)
        {
            mMaxZoom = std::clamp(maxZoom, minZoom, defaultMaxZoom);
            mMaxVerticalFocus = std::max(0.f, std::min(maxVerticalFocus, defaultMaxVerticalFocus));
            setZoom(mZoom);
            setVerticalFocus(mVerticalFocus);
        }

        void resetFraming(float angle = 0.f)
        {
            mRotating = false;
            mFocusing = false;
            mAngularVelocity = 0.f;
            setAngle(angle);
            setZoom(0.f);
            setVerticalFocus(0.f);
        }

        void update(float duration)
        {
            if (mPreview == nullptr || mRotating || mFocusing)
                return;

            const float frameDuration = std::min(std::max(0.f, duration), maxFrameDuration);
            if (frameDuration == 0.f)
                return;

            setAngle(mAngle + (autoRotationSpeed + mAngularVelocity) * frameDuration);

            mAngularVelocity *= std::pow(momentumDecayPerSecond, frameDuration);
            if (std::abs(mAngularVelocity) < stopAngularVelocity)
                mAngularVelocity = 0.f;
        }

    private:
        void setZoom(float zoom)
        {
            mZoom = std::clamp(zoom, minZoom, mMaxZoom);

            if (mPreview != nullptr)
                mPreview->setZoom(mZoom);
        }

        void setVerticalFocus(float focus)
        {
            mVerticalFocus = std::clamp(focus, -mMaxVerticalFocus, mMaxVerticalFocus);

            if (mPreview != nullptr)
                mPreview->setVerticalFocus(mVerticalFocus);
        }

        void onMouseButtonPressed(MyGUI::Widget*, int left, int top, MyGUI::MouseButton id)
        {
            if (id == MyGUI::MouseButton::Left)
            {
                mRotating = true;
                mLastPointerX = left;
                mAngularVelocity = 0.f;
            }
            else if (id == MyGUI::MouseButton::Right)
            {
                mFocusing = true;
                mLastPointerY = top;
            }
        }

        void onMouseButtonReleased(MyGUI::Widget*, int, int, MyGUI::MouseButton id)
        {
            if (id == MyGUI::MouseButton::Left)
                mRotating = false;
            else if (id == MyGUI::MouseButton::Right)
                mFocusing = false;
        }

        void onMouseDrag(MyGUI::Widget*, int left, int top, MyGUI::MouseButton id)
        {
            if (id == MyGUI::MouseButton::Left && mRotating)
            {
                const float deltaAngle = static_cast<float>(left - mLastPointerX) * dragRadiansPerPixel;
                setAngle(mAngle + deltaAngle);
                mAngularVelocity = std::clamp(deltaAngle * dragMomentumFramesPerSecond, -maxAngularVelocity,
                    maxAngularVelocity);
                mLastPointerX = left;
            }
            else if (id == MyGUI::MouseButton::Right && mFocusing)
            {
                setVerticalFocus(mVerticalFocus + static_cast<float>(mLastPointerY - top) * focusUnitsPerPixel);
                mLastPointerY = top;
            }
        }

        void onMouseWheel(MyGUI::Widget*, int rel)
        {
            if (rel == 0)
                return;

            setZoom(mZoom + (rel > 0 ? wheelZoomStep : -wheelZoomStep));
        }

        void onMouseDoubleClick(MyGUI::Widget*)
        {
            mAngularVelocity = 0.f;
            setAngle(0.f);
            setZoom(0.f);
            setVerticalFocus(0.f);
        }

        static constexpr float autoRotationSpeed = 0.18f;
        static constexpr float dragRadiansPerPixel = 0.01f;
        static constexpr float dragMomentumFramesPerSecond = 60.f;
        static constexpr float maxAngularVelocity = 4.f;
        static constexpr float momentumDecayPerSecond = 0.18f;
        static constexpr float stopAngularVelocity = 0.02f;
        static constexpr float maxFrameDuration = 0.1f;
        static constexpr float wheelZoomStep = 0.08f;
        static constexpr float minZoom = 0.f;
        static constexpr float defaultMaxZoom = 1.f;
        static constexpr float focusUnitsPerPixel = 0.35f;
        static constexpr float defaultMaxVerticalFocus = 48.f;
        static constexpr float twoPi = 6.2831853f;

        MyGUI::ImageBox* mImage = nullptr;
        MyGUI::ImageBox* mBoundImage = nullptr;
        MWRender::RaceSelectionPreview* mPreview = nullptr;
        bool mRotating = false;
        bool mFocusing = false;
        int mLastPointerX = 0;
        int mLastPointerY = 0;
        float mAngle = 0.f;
        float mAngularVelocity = 0.f;
        float mZoom = 0.f;
        float mVerticalFocus = 0.f;
        float mMaxZoom = defaultMaxZoom;
        float mMaxVerticalFocus = defaultMaxVerticalFocus;
    };
}

#endif
