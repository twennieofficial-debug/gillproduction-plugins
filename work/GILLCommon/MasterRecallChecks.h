#pragma once
template<class Processor,class Check>
void masterRecallChecks(Processor& p, bool strictSchema, Check checkRecall) {
    p.setValue("boost",0,false);p.setValue("drive",6,false);
    juce::MemoryBlock state;p.getStateInformation(state);
    auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize()));
    auto tree=juce::ValueTree::fromXml(*xml);
    tree.removeChild(tree.getChildWithProperty("id","boost"),nullptr);
    if(strictSchema)tree.setProperty("schema",1,nullptr);
    juce::MemoryBlock legacy;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),legacy);
    p.setValue("drive",12,false);p.setValue("boost",18,false);
    p.setStateInformation(legacy.getData(),int(legacy.getSize()));
    checkRecall(p.value("drive")==6&&p.value("boost")==0,"Legacy state restores original drive and clears extra BOOST");
    p.setValue("boost",9,false);p.getStateInformation(state);p.setValue("boost",0,false);
    p.setStateInformation(state.getData(),int(state.getSize()));
    checkRecall(p.value("boost")==9,"New BOOST survives project state recall");
    const auto& parameters=p.getParameters();
    auto* last=dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters.getLast());
    checkRecall(last&&last->paramID=="boost","BOOST appended after released parameter order");
    p.setValue("boost",0,false);p.setValue("drive",0,false);
}
