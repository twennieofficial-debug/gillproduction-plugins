#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <algorithm>

namespace gill {
// Mouse-operated controls must not swallow SPACE/RETURN intended for the DAW.
// A real text editor retains normal editing focus. Watch dynamically created
// controls as well as those present at construction; never inject OS key events.
class HostKeyboardPolicy final : private juce::ComponentListener {
public:
    explicit HostKeyboardPolicy(juce::Component& editor) { visit(editor); }
    ~HostKeyboardPolicy() override {
        for (auto* c : watched) c->removeComponentListener(this);
    }
private:
    void visit(juce::Component& c) {
        if (dynamic_cast<juce::TextEditor*>(&c) != nullptr) return;
        if (std::find(watched.begin(), watched.end(), &c) == watched.end()) {
            watched.push_back(&c);
            c.addComponentListener(this);
        }
        c.setWantsKeyboardFocus(false);
        c.setMouseClickGrabsKeyboardFocus(false);
        for (auto* child : c.getChildren()) visit(*child);
    }
    void componentChildrenChanged(juce::Component& c) override { visit(c); }
    void componentBeingDeleted(juce::Component& c) override {
        watched.erase(std::remove(watched.begin(), watched.end(), &c), watched.end());
    }
    std::vector<juce::Component*> watched;
};
}
