#include "MacTestSupport.h"
#include "Processor.h"
#include "RackLibrary.h"
#include <iostream>
#include <set>
#include <stdexcept>
void require(bool v, const char *m) {
  if (!v)
    throw std::runtime_error(m);
}
int main() {
  gillInitialiseTestApplication();
  juce::ScopedJuceInitialiser_GUI gui;
  try {
    studio::Processor p(studio::Kind::Rack);
    p.prepareToPlay(48000, 64);
    std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor());
    auto *target = dynamic_cast<juce::DragAndDropTarget *>(e.get());
    require(target, "rack has no drop target");
    const auto &modules = studio::rackCollection;
    std::set<studio::RackFamily> families;
    int compressors = 0;
    for (int m : modules) {
      families.insert(studio::rackModels[m].family);
      if (studio::rackModels[m].family == studio::RackFamily::Comp)
        ++compressors;
    }
    require(families.size() == 16 && compressors == 6,
            "library lacks functional variety");
    for (int module : modules) {
      juce::DragAndDropTarget::SourceDetails d("module:" + juce::String(module),
                                               e.get(), {495, 300});
      require(target->isInterestedInDragSource(d), "module not accepted");
      target->itemDropped(d);
      require(int(p.value("b3_module")) == module,
              "drop inserted into wrong slot");
      juce::ComboBox *pages = nullptr;
      for (auto *c : e->getChildren())
        if (c->getComponentID() == "pages")
          pages = dynamic_cast<juce::ComboBox *>(c);
      require(pages, "detail pages missing");
      for (int page = 1; page <= pages->getNumItems(); ++page) {
        pages->setSelectedId(page, juce::sendNotificationSync);
        for (auto *c : e->getChildren())
          if (c->isVisible() && !dynamic_cast<juce::TooltipWindow *>(c))
            require(e->getLocalBounds().contains(c->getBounds()),
                    "rack control clips");
      }
    }
    p.set("b3_threshold", -31);
    target->itemDropped({"slot:3", e.get(), {270, 200}});
    require(p.value("b3_order") == 1 && p.value("b3_threshold") == -31,
            "reorder lost slot identity");
    target->itemDropped({"module:17", e.get(), {20, 20}});
    require(p.value("b3_module") == 41, "outside drop changed slot");
    juce::MemoryBlock bytes;
    p.getStateInformation(bytes);
    studio::Processor restored(studio::Kind::Rack);
    restored.setStateInformation(bytes.getData(), int(bytes.getSize()));
    require(restored.value("b3_order") == 1 &&
                restored.value("b3_module") == 41,
            "rack recall lost topology");
    const int showcase[]{9, 15, 17, 27, 14, 29, 32, 41};
    for (int i = 1; i <= 8; ++i) {
      p.set("b" + juce::String(i) + "_module", float(showcase[i - 1]));
      p.set("b" + juce::String(i) + "_order", float(i));
    }
    juce::Timer::callPendingTimersSynchronously();
    e.reset(p.createEditor());
    const auto image = e->createComponentSnapshot(e->getLocalBounds());
    auto out = juce::File::getCurrentWorkingDirectory()
                   .getChildFile("GILLRACK.png")
                   .createOutputStream();
    out->setPosition(0);
    out->truncate();
    juce::PNGImageFormat().writeImageToStream(image, *out);
    std::cout << "RACK_30_MODULE_DROP_REORDER_RECALL_PASS\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
