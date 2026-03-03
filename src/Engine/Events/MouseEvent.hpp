#pragma once

#include "Events/Event.hpp"
#include <sstream>

namespace Ayaya {

    class MouseMovedEvent : public Event {
    public:
        MouseMovedEvent(float x, float y) : m_MouseX(x), m_MouseY(y) {}

        inline float GetX() const { return m_MouseX; }
        inline float GetY() const { return m_MouseY; }

        std::string ToString() const override {
            std::stringstream ss;
            ss << "MouseMovedEvent: " << m_MouseX << ", " << m_MouseY;
            return ss.str();
        }

        static EventType GetStaticType() { return EventType::MouseMoved; }
        virtual EventType GetEventType() const override { return GetStaticType(); }
        virtual const char* GetName() const override { return "MouseMoved"; }
        virtual int GetCategoryFlags() const override { return EventCategoryMouse | EventCategoryInput; }

    private:
        float m_MouseX, m_MouseY;
    };

    class MouseScrolledEvent : public Event {
    public:
        MouseScrolledEvent(float xOffset, float yOffset)
            : m_XOffset(xOffset), m_YOffset(yOffset) {}

        inline float GetXOffset() const { return m_XOffset; }
        inline float GetYOffset() const { return m_YOffset; }

        std::string ToString() const override {
            std::stringstream ss;
            ss << "MouseScrolledEvent: " << m_XOffset << ", " << m_YOffset;
            return ss.str();
        }

        static EventType GetStaticType() { return EventType::MouseScrolled; }
        virtual EventType GetEventType() const override { return GetStaticType(); }
        virtual const char* GetName() const override { return "MouseScrolled"; }
        virtual int GetCategoryFlags() const override { return EventCategoryMouse | EventCategoryInput; }

    private:
        float m_XOffset, m_YOffset;
    };

    class MouseButtonEvent : public Event {
    public:
        inline int GetMouseButton() const { return m_Button; }

        virtual int GetCategoryFlags() const override { 
            return EventCategoryMouse | EventCategoryInput | EventCategoryMouseButton; 
        }

    protected:
        MouseButtonEvent(int button) : m_Button(button) {}
        int m_Button;
    };

    class MouseButtonPressedEvent : public MouseButtonEvent {
    public:
        MouseButtonPressedEvent(int button) : MouseButtonEvent(button) {}

        std::string ToString() const override {
            std::stringstream ss;
            ss << "MouseButtonPressedEvent: " << m_Button;
            return ss.str();
        }

        static EventType GetStaticType() { return EventType::MouseButtonPressed; }
        virtual EventType GetEventType() const override { return GetStaticType(); }
        virtual const char* GetName() const override { return "MouseButtonPressed"; }
    };

    class MouseButtonReleasedEvent : public MouseButtonEvent {
    public:
        MouseButtonReleasedEvent(int button) : MouseButtonEvent(button) {}

        std::string ToString() const override {
            std::stringstream ss;
            ss << "MouseButtonReleasedEvent: " << m_Button;
            return ss.str();
        }

        static EventType GetStaticType() { return EventType::MouseButtonReleased; }
        virtual EventType GetEventType() const override { return GetStaticType(); }
        virtual const char* GetName() const override { return "MouseButtonReleased"; }
    };

}