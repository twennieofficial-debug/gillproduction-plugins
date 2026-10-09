#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// These are reviewed editor defaults, independent of the packaging catalog.
// Run immediately after createEditor, before any resize or workflow changes.
namespace gillCurrentUi {
struct Geometry { const char* name; int width, height; };
inline bool visibleInside(juce::Component& root, juce::Component& parent) {
    for (auto* child : parent.getChildren()) {
        if (!child->isVisible()) continue; // hidden subtrees are not interactive
        if (dynamic_cast<juce::Slider*>(child) || dynamic_cast<juce::Button*>(child)
            || dynamic_cast<juce::ComboBox*>(child)) {
            const auto area = root.getLocalArea(child, child->getLocalBounds());
            if (area.isEmpty() || !root.getLocalBounds().contains(area)) return false;
            // A target may be inside the editor yet clipped by an intermediate panel.
            for (auto* ancestor = child->getParentComponent(); ancestor && ancestor != &root;
                 ancestor = ancestor->getParentComponent())
                if (!ancestor->getLocalBounds().contains(ancestor->getLocalArea(child, child->getLocalBounds())))
                    return false;
        }
        if (!visibleInside(root, *child)) return false;
    }
    return true;
}
template<class Check> void boundsOracleControls(Check check) {
    juce::Component root, panel;
    juce::TextButton target;
    root.setSize(100, 100); panel.setBounds(10, 10, 50, 50); target.setBounds(5, 5, 20, 20);
    root.addAndMakeVisible(panel); panel.addAndMakeVisible(target);
    check(visibleInside(root, root), "UI bounds oracle accepts a valid nested control");
    target.setBounds(70, 5, 30, 20);
    check(!visibleInside(root, root), "UI bounds oracle rejects an off-editor control");
    target.setBounds(40, 5, 20, 20);
    check(!visibleInside(root, root), "UI bounds oracle rejects ancestor clipping inside the editor");
    target.setBounds(5, 5, 0, 20);
    check(!visibleInside(root, root), "UI bounds oracle rejects an empty visible target");
    panel.setVisible(false);
    check(visibleInside(root, root), "UI bounds oracle ignores an intentionally hidden subtree");
    panel.setVisible(true); target.setBounds(5, 5, 20, 20);
    panel.setTransform(juce::AffineTransform::scale(1.25f));
    check(visibleInside(root, root), "UI bounds oracle accepts valid transformed descendants");
}
template<class Check> void verify(juce::AudioProcessorEditor* editor, const juce::String& name, Check check) {
    boundsOracleControls(check);
    static constexpr Geometry products[] = {
        {"GILLSILK", 680, 450},
        {"GILLSPARK", 680, 450},
        {"GILLSTRIP", 820, 420},
        {"GILLGOLD", 760, 440},
        {"GILLDIVE", 600, 460}
    };
    const Geometry* expected = nullptr;
    for (const auto& product : products) if (name == product.name) expected = &product;
    check(editor != nullptr && expected != nullptr, "Current UI has an independently reviewed product geometry");
    if (!editor || !expected) return;
    check(editor->getWidth() == expected->width && editor->getHeight() == expected->height,
          "Current UI exact default width and height match the reviewed editor");
    check(visibleInside(*editor, *editor), "Current UI visible standard controls have nonempty bounds inside editor and ancestors");
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.f);
    check(image.isValid() && image.getWidth() == expected->width && image.getHeight() == expected->height,
          "Current UI native default screenshot has exact logical pixel dimensions");
    if (!image.isValid()) return;
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(name + "-UI-default.png");
    juce::FileOutputStream stream(file);
    if (stream.openedOk()) { stream.setPosition(0); stream.truncate(); }
    juce::PNGImageFormat png;
    check(stream.openedOk() && png.writeImageToStream(image, stream), "Current UI fresh native default PNG written");
    stream.flush();
    const auto decoded = juce::ImageFileFormat::loadFrom(file);
    check(decoded.isValid() && decoded.getWidth() == expected->width && decoded.getHeight() == expected->height,
          "Current UI saved PNG decodes at exact reviewed dimensions");
}
}
