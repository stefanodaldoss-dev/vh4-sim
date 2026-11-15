#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class VH4App : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "VH4 Sim"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override { window.reset(new MainWindow(getApplicationName())); }
    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }
private:
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(juce::String name) : DocumentWindow(name, juce::Colour(0xff15171a), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<MainWindow> window;
};
START_JUCE_APPLICATION(VH4App)
