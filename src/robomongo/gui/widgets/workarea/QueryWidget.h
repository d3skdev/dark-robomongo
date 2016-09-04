#pragma once

#include <QWidget>
#include <QDockWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

#include "robomongo/core/Core.h"
#include "robomongo/core/domain/MongoShellResult.h"

namespace Robomongo
{
    class BsonWidget;
    class DocumentListLoadedEvent;
    class ScriptExecutedEvent;
    class AutocompleteResponse;
    class OutputWidget;
    class ScriptWidget;
    class MongoShell;

    class QueryWidget : public QWidget
    {
        Q_OBJECT

    public:
        typedef QWidget BaseClass;
        QueryWidget(MongoShell *shell, QWidget *parent = NULL);

        void toggleOrientation();
        void activateTabContent();
        void openNewTab();
        void reload();
        void duplicate();
        void enterTreeMode();
        void enterTextMode();
        void enterTableMode();
        void enterCustomMode();
        void setScriptFocus();
        void showAutocompletion();
        void hideAutocompletion();

        // Getter
        bool docked() const { return !_dock->isFloating(); }

        ~QueryWidget();

    Q_SIGNALS:
        void titleChanged(const QString &text);
        void toolTipChanged(const QString &text);

    public Q_SLOTS:
        void execute();
        void stop();

        void saveToFile();
        void savebToFileAs();
        void openFile();
        void textChange();
        void showProgress();
    public Q_SLOTS:
        void handle(DocumentListLoadedEvent *event);
        void handle(ScriptExecutedEvent *event);
        void handle(AutocompleteResponse *event);

    private Q_SLOTS:
        void on_dock_undock(bool isFloating);

    private:        
        void hideProgress();
        void updateCurrentTab();
        void displayData(const std::vector<MongoShellResult> &results, bool empty);

        MongoShell *_shell;
        OutputWidget *_viewer;
        ScriptWidget *_scriptWidget;
        QLabel *_outputLabel;
        QDockWidget *_dock;

        MongoShellExecResult _currentResult;
        bool _isTextChanged;
    };
}
