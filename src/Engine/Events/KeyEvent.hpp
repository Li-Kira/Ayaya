#pragma once

#include "Events/Event.hpp"
#include <sstream>

namespace Ayaya {

    class KeyEvent : public Event {
    public:
        inline int GetKeyCode() const { return m_KeyCode; }

        virtual int GetCategoryFlags() const override { 
            return EventCategoryKeyboard | EventCategoryInput; 
        }

    protected:
        KeyEvent(int keycode) : m_KeyCode(keycode) {}
        int m_KeyCode;
    };

    class KeyPressedEvent : public KeyEvent {
    public:
        KeyPressedEvent(int keycode, int repeatCount)
            : KeyEvent(keycode), m_RepeatCount(repeatCount) {}

        inline int GetRepeatCount() const { return m_RepeatCount; }

        std::string ToString() const override {
            std::stringstream ss;
            ss << "KeyPressedEvent: " << m_KeyCode << " (" << m_RepeatCount << " repeats)";
            return ss.str();
        }

        static EventType GetStaticType() { return EventType::KeyPressed; }
        virtual EventType GetEventType() const override { return GetStaticType(); }
        virtual const char* GetName() const override { return "KeyPressed"; }

    private:
        int m_RepeatCount;
    };

    class KeyReleasedEvent : public KeyEvent {
    public:
        KeyReleasedEvent(int keycode) : KeyEvent(keycode) {}

        std::string ToString() const override {
            std::stringstream ss;
            ss << "KeyReleasedEvent: " << m_KeyCode;
            return ss.str();
        }

        static EventType GetStaticType() { return EventType::KeyReleased; }
        virtual EventType GetEventType() const override { return GetStaticType(); }
        virtual const char* GetName() const override { return "KeyReleased"; }
    };

    class KeyTypedEvent : public KeyEvent {
    public:
        KeyTypedEvent(int keycode) : KeyEvent(keycode) {}

        std::string ToString() const override {
            std::stringstream ss;
            ss << "KeyTypedEvent: " << m_KeyCode;
            return ss.str();
        }

        static EventType GetStaticType() { return EventType::KeyTyped; }
        virtual EventType GetEventType() const override { return GetStaticType(); }
        virtual const char* GetName() const override { return "KeyTyped"; }
    };
}