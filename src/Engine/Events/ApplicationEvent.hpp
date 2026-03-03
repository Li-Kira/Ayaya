#pragma once
#include "Event.hpp"

namespace Ayaya {

    class WindowResizeEvent : public Event {
    public:
        WindowResizeEvent(uint32_t width, uint32_t height)
            : m_Width(width), m_Height(height) {}

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

        static EventType GetStaticType() { return EventType::WindowResize; }
        virtual EventType GetEventType() const override { return GetStaticType(); }
        virtual const char* GetName() const override { return "WindowResize"; }
        virtual int GetCategoryFlags() const override { return EventCategoryApplication; }

    private:
        uint32_t m_Width, m_Height;
    };

    class WindowCloseEvent : public Event {
    public:
        WindowCloseEvent() = default;
        static EventType GetStaticType() { return EventType::WindowClose; }
        virtual EventType GetEventType() const override { return GetStaticType(); }
        virtual const char* GetName() const override { return "WindowClose"; }
        virtual int GetCategoryFlags() const override { return EventCategoryApplication; }
    };
}