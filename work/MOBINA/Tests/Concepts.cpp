#include "../Source/Theme.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
int main(int argc,char**argv){juce::ScopedJuceInitialiser_GUI gui;juce::File root(argc>1?argv[1]:"C:/Users/iangi/Documents/Jill Plugins/Designs/Familien 01");root.createDirectory();
 const char* families[]{"MASTER","SMART","CLEANER"};const char* names[3][5]{{"MIDNIGHT COBALT","BORDEAUX CHROME","INK BLUE","GARNET STEEL","NAVY CHAMPAGNE"},{"PETROL INTELLIGENCE","AMETHYST LEARN","DEEP TEAL","SMOKE VIOLET","SAPPHIRE SIGNAL"},{"GLACIER BLUE","SAGE SILVER","ARCTIC TEAL","PEARL LAVENDER","OCEAN STEEL"}};
 const float hues[3][5]{{.63f,.97f,.67f,.94f,.60f},{.51f,.76f,.46f,.79f,.61f},{.56f,.40f,.50f,.74f,.58f}};
 const float sats[3][5]{{1,.84f,.65f,.75f,.6f},{.9f,.65f,.8f,.48f,.83f},{.65f,.30f,.48f,.24f,.42f}};
 for(int family=0;family<3;++family)for(int v=0;v<5;++v){using namespace mobina::skin;palette(hues[family][v],sats[family][v],family==2?1.5f:.87f);int w=family==2?550:820,h=family==2?420:555;juce::Image image(juce::Image::ARGB,w*2,h*2,true,juce::SoftwareImageType());juce::Graphics g(image);g.addTransform(juce::AffineTransform::scale(2));chassis(g,float(w),float(h),family==2?187.f:230.f,57,true,40,.2f);title(g,family==0?"GILLMAX":family==1?"GILLBALANCE":"GILLDECLICK",{72,16,250,30},23);
 g.setFont(font(11,true));g.setColour(white);g.drawText("LIVE  |  PRO",w-130,24,102,20,juce::Justification::centred);g.setFont(font(11));g.setColour(white.withAlpha(.65f));g.drawText(juce::String(families[family])+"  /  "+names[family][v],27,73,w-50,24,juce::Justification::left);
 glass(g,{25,110,float(w-50),float(family==2?64:104)});juce::Path wave;for(int i=0;i<w-60;++i){float y=family==2?143:168;if(family==0)y+=float(std::sin(i*.052)*12+std::sin(i*.119)*5);else if(family==1)y-=float(20*std::exp(-std::pow((i-410)/85.,2))-14*std::exp(-std::pow((i-180)/40.,2)));else y+=float(std::sin(i*.17)*std::exp(-std::pow((i-220)/75.,2))*20);if(i==0)wave.startNewSubPath(30.f+i,y);else wave.lineTo(30.f+i,y);}electricTrace(g,wave,cyan,.6f,1.2f);
 if(family==2){material::rotary(g,{201,224,146,146},.38f,3.8f,8.75f,cyan);g.setColour(ink);g.setFont(font(12,true));g.drawText("REMOVE",208,198,130,20,juce::Justification::centred);g.drawText("38 %",208,367,130,20,juce::Justification::centred);}
 else{const char* master[]{"DRIVE","CEILING","RELEASE","TRANSIENT","SOFT CLIP","MIX"};const char* smart[]{"AMOUNT","WARMTH","PRESENCE","AIR","SMOOTH","MIX"};for(int i=0;i<6;++i){int x=28+i*128;material::rotary(g,{float(x),285,114,114},.26f+i*.08f,3.8f,8.75f,cyan);g.setColour(ink);g.setFont(font(11,true));g.drawText(family==0?master[i]:smart[i],x,258,115,20,juce::Justification::centred);g.drawText(juce::String(26+i*8)+" %",x,401,115,20,juce::Justification::centred);}material::panel(g,{280,445,260,41},navy,8);g.setColour(white);g.setFont(font(12,true));g.drawText(family==0?"INTEGRATED  -10.4 LUFS":"LEARN  /  MODERN VOCAL",280,449,260,30,juce::Justification::centred);}
 auto file=root.getChildFile(juce::String(families[family])+"-0"+juce::String(v+1)+".png");auto stream=file.createOutputStream();stream->setPosition(0);stream->truncate();juce::PNGImageFormat png;png.writeImageToStream(image,*stream);std::cout<<file.getFullPathName()<<"\n";}
 std::cout<<"FIFTEEN_CONCEPTS_RENDERED\n";
}


