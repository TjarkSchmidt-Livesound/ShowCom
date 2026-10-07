#include "AppController.h"
#include "ui/MainComponent.h"

class ShowComApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& commandLine) override
    {
        controller = std::make_unique<AppController>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *controller);

        if (commandLine.contains ("--demo"))
            controller->injectDemoConversation();
    }

    void shutdown() override
    {
        mainWindow = nullptr;   // GUI zuerst, dann Audio/Transkription
        controller = nullptr;
    }

    void systemRequestedQuit() override
    {
        if (controller != nullptr && controller->isSessionRunning())
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                              .withTitle (juce::String::fromUTF8 ("Session läuft"))
                                              .withMessage (juce::String::fromUTF8 ("Eine Session läuft noch. Mitschnitt beenden und Programm schließen?"))
                                              .withButton ("Beenden")
                                              .withButton ("Abbrechen"),
                                          [] (int result)
                                          {
                                              if (result == 1)
                                                  JUCEApplication::getInstance()->quit();
                                          });
            return;
        }

        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, AppController& app)
            : DocumentWindow (name, juce::Colour (0xff111317), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (app), true);
            setResizable (true, true);
            setResizeLimits (900, 600, 10000, 10000);

            if (! restoreWindowStateFromString (loadWindowState()))
                centreWithSize (getWidth(), getHeight());

            setVisible (true);
        }

        ~MainWindow() override
        {
            saveWindowState (getWindowStateAsString());
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        static juce::File windowStateFile()     { return AppState::getAppDataDir().getChildFile ("window.txt"); }
        static juce::String loadWindowState()   { return windowStateFile().loadFileAsString(); }
        static void saveWindowState (const juce::String& s) { windowStateFile().replaceWithText (s); }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<AppController> controller;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ShowComApplication)
