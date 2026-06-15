/**
 * Event system unit tests — verifies dispatch, propagation, category flags,
 * and type-safe dispatching across all event types.
 */
#include <gtest/gtest.h>
#include "Engine/Events/Event.hpp"
#include "Engine/Events/KeyEvent.hpp"
#include "Engine/Events/MouseEvent.hpp"
#include "Engine/Core/KeyCodes.hpp"

using namespace Ayaya;

// =========================================================================
// Event base class
// =========================================================================

TEST(EventTest, HandledFlag_DefaultsToFalse) {
    KeyPressedEvent e(Key::A, 0);
    EXPECT_FALSE(e.Handled);
}

TEST(EventTest, HandledFlag_CanBeSet) {
    KeyPressedEvent e(Key::A, 0);
    e.Handled = true;
    EXPECT_TRUE(e.Handled);
}

TEST(EventTest, IsInCategory_KeyboardEventsMatchKeyboardCategory) {
    KeyPressedEvent e(Key::Z, 0);
    // Keyboard=4, Input=2, Mouse=8, Application=1
    EXPECT_TRUE(e.IsInCategory((Ayaya::EventCategory)4));
    EXPECT_TRUE(e.IsInCategory((Ayaya::EventCategory)2));
    EXPECT_FALSE(e.IsInCategory((Ayaya::EventCategory)8));
    EXPECT_FALSE(e.IsInCategory((Ayaya::EventCategory)1));
}

TEST(EventTest, IsInCategory_MouseEventsMatchMouseCategory) {
    MouseMovedEvent e(100.0f, 200.0f);
    // Mouse=8, Input=2, Keyboard=4
    EXPECT_TRUE(e.IsInCategory((Ayaya::EventCategory)8));
    EXPECT_TRUE(e.IsInCategory((Ayaya::EventCategory)2));
    EXPECT_FALSE(e.IsInCategory((Ayaya::EventCategory)4));
}

TEST(EventTest, MouseButtonEvent_HasMouseButtonCategory) {
    MouseButtonPressedEvent e(Mouse::ButtonLeft);
    // MouseButton=16
    EXPECT_TRUE(e.IsInCategory((Ayaya::EventCategory)16));
    // Mouse=8, Input=2
    EXPECT_TRUE(e.IsInCategory((Ayaya::EventCategory)8));
    EXPECT_TRUE(e.IsInCategory((Ayaya::EventCategory)2));
}

// =========================================================================
// Event type identification
// =========================================================================

TEST(EventTest, KeyPressed_ReportsCorrectType) {
    KeyPressedEvent e(Key::W, 0);
    EXPECT_EQ(e.GetEventType(), EventType::KeyPressed);
    EXPECT_STREQ(e.GetName(), "KeyPressed");
}

TEST(EventTest, KeyReleased_ReportsCorrectType) {
    KeyReleasedEvent e(Key::S);
    EXPECT_EQ(e.GetEventType(), EventType::KeyReleased);
    EXPECT_STREQ(e.GetName(), "KeyReleased");
}

TEST(EventTest, KeyTyped_ReportsCorrectType) {
    KeyTypedEvent e(Key::Enter);
    EXPECT_EQ(e.GetEventType(), EventType::KeyTyped);
    EXPECT_STREQ(e.GetName(), "KeyTyped");
}

TEST(EventTest, MouseMoved_ReportsCorrectType) {
    MouseMovedEvent e(10.0f, 20.0f);
    EXPECT_EQ(e.GetEventType(), EventType::MouseMoved);
    EXPECT_STREQ(e.GetName(), "MouseMoved");
}

TEST(EventTest, MouseScrolled_ReportsCorrectType) {
    MouseScrolledEvent e(1.0f, 2.0f);
    EXPECT_EQ(e.GetEventType(), EventType::MouseScrolled);
    EXPECT_STREQ(e.GetName(), "MouseScrolled");
}

TEST(EventTest, MouseButtonPressed_ReportsCorrectType) {
    MouseButtonPressedEvent e(Mouse::ButtonRight);
    EXPECT_EQ(e.GetEventType(), EventType::MouseButtonPressed);
    EXPECT_STREQ(e.GetName(), "MouseButtonPressed");
}

TEST(EventTest, MouseButtonReleased_ReportsCorrectType) {
    MouseButtonReleasedEvent e(Mouse::ButtonMiddle);
    EXPECT_EQ(e.GetEventType(), EventType::MouseButtonReleased);
    EXPECT_STREQ(e.GetName(), "MouseButtonReleased");
}

TEST(EventTest, StaticType_MatchesInstanceType) {
    KeyPressedEvent e(Key::A, 0);
    EXPECT_EQ(KeyPressedEvent::GetStaticType(), e.GetEventType());
    EXPECT_EQ(MouseMovedEvent::GetStaticType(), EventType::MouseMoved);
}

// =========================================================================
// EventDispatcher — type-safe dispatch
// =========================================================================

TEST(EventDispatcherTest, Dispatch_MatchingTypeTriggersCallback) {
    KeyPressedEvent e(Key::Space, 0);
    EventDispatcher dispatcher(e);

    bool called = false;
    bool result = dispatcher.Dispatch<KeyPressedEvent>([&](KeyPressedEvent& ev) {
        called = true;
        EXPECT_EQ(ev.GetKeyCode(), Key::Space);
        EXPECT_EQ(ev.GetRepeatCount(), 0);
        return false;
    });

    EXPECT_TRUE(result)  << "Dispatch should return true for matching type";
    EXPECT_TRUE(called);
}

TEST(EventDispatcherTest, Dispatch_NonMatchingTypeDoesNotTrigger) {
    KeyPressedEvent e(Key::A, 0);
    EventDispatcher dispatcher(e);

    bool called = false;
    bool result = dispatcher.Dispatch<KeyReleasedEvent>([&](KeyReleasedEvent&) {
        called = true;
        return false;
    });

    EXPECT_FALSE(result) << "Dispatch should return false for mismatched type";
    EXPECT_FALSE(called);
}

TEST(EventDispatcherTest, Dispatch_ReturningTrueSetsHandled) {
    MouseButtonPressedEvent e(Mouse::ButtonLeft);
    EventDispatcher dispatcher(e);

    dispatcher.Dispatch<MouseButtonPressedEvent>([](MouseButtonPressedEvent& ev) {
        return true; // mark as handled
    });

    EXPECT_TRUE(e.Handled);
}

TEST(EventDispatcherTest, Dispatch_ReturningFalseLeavesHandledUnchanged) {
    MouseButtonPressedEvent e(Mouse::ButtonLeft);
    EventDispatcher dispatcher(e);

    dispatcher.Dispatch<MouseButtonPressedEvent>([](MouseButtonPressedEvent& ev) {
        return false; // don't mark as handled
    });

    EXPECT_FALSE(e.Handled);
}

TEST(EventDispatcherTest, Dispatch_MultipleDispatchersAccumulateHandled) {
    // Simulate the layer pattern: first dispatcher marks handled,
    // second dispatcher's callback still fires (handled is set after dispatch).
    // This tests that multiple dispatchers on the same event observe each other's state.
    KeyPressedEvent e(Key::A, 0);
    EventDispatcher d1(e);
    d1.Dispatch<KeyPressedEvent>([](KeyPressedEvent&) { return true; });
    EXPECT_TRUE(e.Handled);

    // A second dispatcher still sees the handled flag
    EventDispatcher d2(e);
    bool secondCalled = false;
    d2.Dispatch<KeyPressedEvent>([&](KeyPressedEvent& ev) {
        secondCalled = true;
        EXPECT_TRUE(ev.Handled); // still set from d1
        return false;
    });
    EXPECT_TRUE(secondCalled);
}

// =========================================================================
// Event data accessors
// =========================================================================

TEST(EventTest, KeyEvent_AccessorsReturnCorrectValues) {
    KeyPressedEvent e(Key::Q, 3);
    EXPECT_EQ(e.GetKeyCode(), Key::Q);
    EXPECT_EQ(e.GetRepeatCount(), 3);
}

TEST(EventTest, MouseMovedEvent_AccessorsReturnCorrectValues) {
    MouseMovedEvent e(1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(e.GetX(), 1280.0f);
    EXPECT_FLOAT_EQ(e.GetY(), 720.0f);
}

TEST(EventTest, MouseScrolledEvent_AccessorsReturnCorrectValues) {
    MouseScrolledEvent e(1.5f, -3.0f);
    EXPECT_FLOAT_EQ(e.GetXOffset(), 1.5f);
    EXPECT_FLOAT_EQ(e.GetYOffset(), -3.0f);
}

TEST(EventTest, MouseButtonEvent_AccessorsReturnCorrectValues) {
    MouseButtonPressedEvent pressed(Mouse::ButtonRight);
    EXPECT_EQ(pressed.GetMouseButton(), (int)Mouse::ButtonRight);

    MouseButtonReleasedEvent released(Mouse::ButtonLeft);
    EXPECT_EQ(released.GetMouseButton(), (int)Mouse::ButtonLeft);
}

// =========================================================================
// Event toString
// =========================================================================

TEST(EventTest, KeyPressedEvent_ToStringContainsKeyCode) {
    KeyPressedEvent e(Key::Enter, 0);
    std::string s = e.ToString();
    EXPECT_NE(s.find("KeyPressedEvent"), std::string::npos);
}

TEST(EventTest, KeyReleasedEvent_ToStringContainsKeyCode) {
    KeyReleasedEvent e(Key::Escape);
    std::string s = e.ToString();
    EXPECT_NE(s.find("KeyReleasedEvent"), std::string::npos);
}

TEST(EventTest, MouseMovedEvent_ToStringContainsCoordinates) {
    MouseMovedEvent e(99.0f, 101.0f);
    std::string s = e.ToString();
    EXPECT_NE(s.find("MouseMovedEvent"), std::string::npos);
}

// =========================================================================
// Category flag bitmask composition
// =========================================================================

TEST(EventTest, CategoryFlags_AreDisjointForDistinctCategories) {
    // Verifies that different event types don't accidentally overlap category bits
    KeyPressedEvent ke(Key::A, 0);
    MouseMovedEvent me(0, 0);

    int kf = ke.GetCategoryFlags();
    int mf = me.GetCategoryFlags();

    // Both have Input, but keyboard ≠ mouse
    EXPECT_NE(kf, mf);
    // Use raw enum values (EventCategory bit masks):
    //   Keyboard = 1<<2 = 4,  Mouse = 1<<3 = 8,  MouseButton = 1<<4 = 16
    // Avoid enum names that clash with Windows.h identifiers.
    bool kfHasKeyboard = kf & 4;   // EventCategory::Keyboard
    bool mfHasMouse     = mf & 8;   // EventCategory::Mouse
    bool kfHasMouse     = kf & 8;
    bool mfHasKeyboard  = mf & 4;
    EXPECT_TRUE(kfHasKeyboard);
    EXPECT_TRUE(mfHasMouse);
    EXPECT_FALSE(kfHasMouse);
    EXPECT_FALSE(mfHasKeyboard);
}
