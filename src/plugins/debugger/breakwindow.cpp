/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "breakwindow.h"
#include "breakhandler.h"

#include "debuggeractions.h"
#include "debuggerplugin.h"
#include "debuggerconstants.h"
#include "ui_breakpoint.h"
#include "ui_breakcondition.h"

#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/savedaction.h>

#include <QtCore/QDebug>

#include <QtGui/QAction>
#include <QtGui/QHeaderView>
#include <QtGui/QKeyEvent>
#include <QtGui/QMenu>
#include <QtGui/QResizeEvent>
#include <QtGui/QItemSelectionModel>
#include <QtGui/QToolButton>
#include <QtGui/QTreeView>
#include <QtGui/QIntValidator>


namespace Debugger {
namespace Internal {

static DebuggerPlugin *plugin()
{
    return DebuggerPlugin::instance();
}

static BreakHandler *breakHandler()
{
    return plugin()->breakHandler();
}

static BreakpointData *breakpointAt(int index)
{
    BreakHandler *handler = breakHandler();
    QTC_ASSERT(handler, return 0);
    return handler->at(index);
}

static void synchronizeBreakpoints()
{
    BreakHandler *handler = breakHandler();
    QTC_ASSERT(handler, return);
    handler->synchronizeBreakpoints();
}

static void appendBreakpoint(BreakpointData *data)
{
    BreakHandler *handler = breakHandler();
    QTC_ASSERT(handler, return);
    handler->appendBreakpoint(data);
}


///////////////////////////////////////////////////////////////////////
//
// BreakpointDialog
//
///////////////////////////////////////////////////////////////////////

class BreakpointDialog : public QDialog, public Ui::BreakpointDialog
{
    Q_OBJECT
public:
    explicit BreakpointDialog(QWidget *parent);
    bool showDialog(BreakpointData *data);

public slots:
    void typeChanged(int index);
};

BreakpointDialog::BreakpointDialog(QWidget *parent) : QDialog(parent)
{
    setupUi(this);
    comboBoxType->insertItem(0, tr("File and Line Number"));
    comboBoxType->insertItem(1, tr("Function Name"));
    comboBoxType->insertItem(2, tr("Function \"main()\""));
    comboBoxType->insertItem(3, tr("Address"));
    pathChooserFileName->setExpectedKind(Utils::PathChooser::File);
    connect(comboBoxType, SIGNAL(activated(int)), SLOT(typeChanged(int)));
    lineEditIgnoreCount->setValidator(
        new QIntValidator(0, 2147483647, lineEditIgnoreCount));
}

bool BreakpointDialog::showDialog(BreakpointData *data)
{
    pathChooserFileName->setPath(data->fileName);
    lineEditLineNumber->setText(QString::number(data->lineNumber));
    lineEditFunction->setText(data->funcName);
    lineEditCondition->setText(QString::fromUtf8(data->condition));
    lineEditIgnoreCount->setText(QString::number(data->ignoreCount));
    checkBoxUseFullPath->setChecked(data->useFullPath);
    lineEditThreadSpec->setText(QString::fromUtf8(data->threadSpec));
    if (data->address)
        lineEditAddress->setText(QString::fromAscii("0x%1").arg(data->address, 0, 16));
    int initialType = 0;
    if (!data->funcName.isEmpty())
        initialType = data->funcName == QLatin1String("main") ? 2 : 1;
    if (data->address)
        initialType = 3;
    typeChanged(initialType);

    if (exec() != QDialog::Accepted)
        return false;

    // Check if changed.
    const int newLineNumber = lineEditLineNumber->text().toInt();
    const bool newUseFullPath  = checkBoxUseFullPath->isChecked();
    const quint64 newAddress = lineEditAddress->text().toULongLong(0, 0);
    const QString newFunc = lineEditFunction->text();
    const QString newFileName = pathChooserFileName->path();
    const QByteArray newCondition = lineEditCondition->text().toUtf8();
    const int newIgnoreCount = lineEditIgnoreCount->text().toInt();
    const QByteArray newThreadSpec = lineEditThreadSpec->text().toUtf8();
    if (newLineNumber == data->lineNumber && newUseFullPath == data->useFullPath
        && newAddress == data->address && newFunc == data->funcName
        && newFileName == data->fileName && newCondition == data->condition
        && newIgnoreCount == data->ignoreCount && newThreadSpec == data->threadSpec)
        return false; // Unchanged -> Cancel.

    data->address = newAddress;
    data->funcName = newFunc;
    data->useFullPath = newUseFullPath;
    data->fileName = newFileName;
    data->lineNumber = newLineNumber;
    data->condition = newCondition;
    data->ignoreCount = newIgnoreCount;
    data->threadSpec = newThreadSpec;
    return true;
}

void BreakpointDialog::typeChanged(int index)
{
    const bool isLineVisible = index == 0;
    const bool isFunctionVisible = index == 1;
    const bool isAddressVisible = index == 3;
    labelFileName->setEnabled(isLineVisible);
    pathChooserFileName->setEnabled(isLineVisible);
    labelLineNumber->setEnabled(isLineVisible);
    lineEditLineNumber->setEnabled(isLineVisible);
    labelUseFullPath->setEnabled(isLineVisible);
    checkBoxUseFullPath->setEnabled(isLineVisible);
    labelFunction->setEnabled(isFunctionVisible);
    lineEditFunction->setEnabled(isFunctionVisible);
    labelAddress->setEnabled(isAddressVisible);
    lineEditAddress->setEnabled(isAddressVisible);
    if (index == 2)
        lineEditFunction->setText(QLatin1String("main"));
}

///////////////////////////////////////////////////////////////////////
//
// BreakWindow
//
///////////////////////////////////////////////////////////////////////

BreakWindow::BreakWindow(QWidget *parent)
  : QTreeView(parent)
{
    m_alwaysResizeColumnsToContents = false;

    QAction *act = theDebuggerAction(UseAlternatingRowColors);
    setFrameStyle(QFrame::NoFrame);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setWindowTitle(tr("Breakpoints"));
    setWindowIcon(QIcon(QLatin1String(":/debugger/images/debugger_breakpoints.png")));
    setAlternatingRowColors(act->isChecked());
    setRootIsDecorated(false);
    setIconSize(QSize(10, 10));
    setSelectionMode(QAbstractItemView::ExtendedSelection);

    connect(this, SIGNAL(activated(QModelIndex)),
        this, SLOT(rowActivated(QModelIndex)));
    connect(act, SIGNAL(toggled(bool)),
        this, SLOT(setAlternatingRowColorsHelper(bool)));
    connect(theDebuggerAction(UseAddressInBreakpointsView), SIGNAL(toggled(bool)),
        this, SLOT(showAddressColumn(bool)));
}

BreakWindow::~BreakWindow()
{
}

void BreakWindow::showAddressColumn(bool on)
{
    setColumnHidden(7, !on);
}

static QModelIndexList normalizeIndexes(const QModelIndexList &list)
{
    QModelIndexList res;
    foreach (const QModelIndex &index, list)
        if (index.column() == 0)
            res.append(index);
    return res;
}

void BreakWindow::keyPressEvent(QKeyEvent *ev)
{
    if (ev->key() == Qt::Key_Delete) {
        QItemSelectionModel *sm = selectionModel();
        QTC_ASSERT(sm, return);
        QModelIndexList si = sm->selectedIndexes();
        if (si.isEmpty())
            si.append(currentIndex().sibling(currentIndex().row(), 0));
        deleteBreakpoints(normalizeIndexes(si));
    }
    QTreeView::keyPressEvent(ev);
}

void BreakWindow::resizeEvent(QResizeEvent *ev)
{
    QTreeView::resizeEvent(ev);
}

void BreakWindow::mouseDoubleClickEvent(QMouseEvent *ev)
{
    QModelIndex indexUnderMouse = indexAt(ev->pos());
    if (indexUnderMouse.isValid() && indexUnderMouse.column() >= 4)
        editBreakpoints(QModelIndexList() << indexUnderMouse);
    QTreeView::mouseDoubleClickEvent(ev);
}

void BreakWindow::contextMenuEvent(QContextMenuEvent *ev)
{
    QMenu menu;
    QItemSelectionModel *sm = selectionModel();
    QTC_ASSERT(sm, return);
    QModelIndexList si = sm->selectedIndexes();
    QModelIndex indexUnderMouse = indexAt(ev->pos());
    if (si.isEmpty() && indexUnderMouse.isValid())
        si.append(indexUnderMouse.sibling(indexUnderMouse.row(), 0));
    si = normalizeIndexes(si);

    const int rowCount = model()->rowCount();
    const unsigned engineCapabilities = BreakOnThrowAndCatchCapability;
    // FIXME BP:    model()->data(QModelIndex(), EngineCapabilitiesRole).toUInt();

    QAction *deleteAction = new QAction(tr("Delete Breakpoint"), &menu);
    deleteAction->setEnabled(si.size() > 0);

    QAction *deleteAllAction = new QAction(tr("Delete All Breakpoints"), &menu);
    deleteAllAction->setEnabled(model()->rowCount() > 0);

    // Delete by file: Find indices of breakpoints of the same file.
    QAction *deleteByFileAction = 0;
    QList<int> breakPointsOfFile;
    if (indexUnderMouse.isValid()) {
        const QModelIndex index = indexUnderMouse.sibling(indexUnderMouse.row(), 2);
        const QString file = model()->data(index).toString();
        if (!file.isEmpty()) {
            for (int i = 0; i < rowCount; i++)
                if (model()->data(model()->index(i, 2)).toString() == file)
                    breakPointsOfFile.push_back(i);
            if (breakPointsOfFile.size() > 1) {
                deleteByFileAction =
                    new QAction(tr("Delete Breakpoints of \"%1\"").arg(file), &menu);
                deleteByFileAction->setEnabled(true);
            }
        }
    }
    if (!deleteByFileAction) {
        deleteByFileAction = new QAction(tr("Delete Breakpoints of File"), &menu);
        deleteByFileAction->setEnabled(false);
    }

    QAction *adjustColumnAction =
        new QAction(tr("Adjust Column Widths to Contents"), &menu);

    QAction *alwaysAdjustAction =
        new QAction(tr("Always Adjust Column Widths to Contents"), &menu);

    alwaysAdjustAction->setCheckable(true);
    alwaysAdjustAction->setChecked(m_alwaysResizeColumnsToContents);

    QAction *editBreakpointAction =
        new QAction(tr("Edit Breakpoint..."), &menu);
    editBreakpointAction->setEnabled(si.size() > 0);

    int threadId = 0;
    // FIXME BP: m_engine->threadsHandler()->currentThreadId();
    QString associateTitle = threadId == -1
        ?  tr("Associate Breakpoint With All Threads")
        :  tr("Associate Breakpoint With Thread %1").arg(threadId);
    QAction *associateBreakpointAction = new QAction(associateTitle, &menu);
    associateBreakpointAction->setEnabled(si.size() > 0);

    QAction *synchronizeAction =
        new QAction(tr("Synchronize Breakpoints"), &menu);
    synchronizeAction->setEnabled(plugin()->hasSnapshots());

    QModelIndex idx0 = (si.size() ? si.front() : QModelIndex());
    QModelIndex idx2 = idx0.sibling(idx0.row(), 2);
    BreakpointData *data = breakpointAt(idx0.row());
    bool enabled = si.isEmpty() || (data && data->enabled);

    const QString str5 = si.size() > 1
        ? enabled
            ? tr("Disable Selected Breakpoints")
            : tr("Enable Selected Breakpoints")
        : enabled
            ? tr("Disable Breakpoint")
            : tr("Enable Breakpoint");
    QAction *toggleEnabledAction = new QAction(str5, &menu);
    toggleEnabledAction->setEnabled(si.size() > 0);

    QAction *addBreakpointAction =
        new QAction(tr("Add Breakpoint..."), this);
    QAction *breakAtThrowAction =
        new QAction(tr("Set Breakpoint at \"throw\""), this);
    QAction *breakAtCatchAction =
        new QAction(tr("Set Breakpoint at \"catch\""), this);

    menu.addAction(addBreakpointAction);
    menu.addAction(deleteAction);
    menu.addAction(editBreakpointAction);
    menu.addAction(associateBreakpointAction);
    menu.addAction(toggleEnabledAction);
    menu.addSeparator();
    menu.addAction(deleteAllAction);
    menu.addAction(deleteByFileAction);
    menu.addSeparator();
    menu.addAction(synchronizeAction);
    if (engineCapabilities & BreakOnThrowAndCatchCapability) {
        menu.addSeparator();
        menu.addAction(breakAtThrowAction);
        menu.addAction(breakAtCatchAction);
    }
    menu.addSeparator();
    menu.addAction(theDebuggerAction(UseToolTipsInBreakpointsView));
    menu.addAction(theDebuggerAction(UseAddressInBreakpointsView));
    menu.addAction(adjustColumnAction);
    menu.addAction(alwaysAdjustAction);
    menu.addSeparator();
    menu.addAction(theDebuggerAction(SettingsDialog));

    QAction *act = menu.exec(ev->globalPos());

    if (act == deleteAction) {
        deleteBreakpoints(si);
    } else if (act == deleteAllAction) {
        QList<int> allRows;
        for (int i = 0; i < rowCount; i++)
            allRows.push_back(i);
        deleteBreakpoints(allRows);
    }  else if (act == deleteByFileAction)
        deleteBreakpoints(breakPointsOfFile);
    else if (act == adjustColumnAction)
        resizeColumnsToContents();
    else if (act == alwaysAdjustAction)
        setAlwaysResizeColumnsToContents(!m_alwaysResizeColumnsToContents);
    else if (act == editBreakpointAction)
        editBreakpoints(si);
    else if (act == associateBreakpointAction)
        associateBreakpoint(si, threadId);
    else if (act == synchronizeAction)
        synchronizeBreakpoints();
    else if (act == toggleEnabledAction)
        setBreakpointsEnabled(si, !enabled);
    else if (act == addBreakpointAction)
        addBreakpoint();
    else if (act == breakAtThrowAction) {
        BreakpointData *data = new BreakpointData;
        data->funcName = BreakpointData::throwFunction;
        appendBreakpoint(data);
    } else if (act == breakAtCatchAction) {
        BreakpointData *data = new BreakpointData;
        data->funcName = BreakpointData::catchFunction;
        appendBreakpoint(data);
    }
}

void BreakWindow::setBreakpointsEnabled(const QModelIndexList &list, bool enabled)
{
    foreach (const QModelIndex &index, list) {
        BreakpointData *data = breakpointAt(index.row());
        QTC_ASSERT(data, continue);
        data->enabled = enabled;
    }
    synchronizeBreakpoints();
}

void BreakWindow::setBreakpointsFullPath(const QModelIndexList &list, bool fullpath)
{
    foreach (const QModelIndex &index, list) {
        BreakpointData *data = breakpointAt(index.row());
        QTC_ASSERT(data, continue);
        data->useFullPath = fullpath;
    }
    synchronizeBreakpoints();
}

void BreakWindow::deleteBreakpoints(const QModelIndexList &indexes)
{
    QTC_ASSERT(!indexes.isEmpty(), return);
    QList<int> list;
    foreach (const QModelIndex &index, indexes)
        list.append(index.row());
    deleteBreakpoints(list);
}

void BreakWindow::deleteBreakpoints(QList<int> list)
{
    if (list.empty())
        return;
    BreakHandler *handler = breakHandler();
    const int firstRow = list.front();
    qSort(list.begin(), list.end());
    for (int i = list.size(); --i >= 0; ) {
        BreakpointData *data = breakpointAt(i);
        QTC_ASSERT(data, continue);
        handler->removeBreakpoint(data);
    }

    const int row = qMin(firstRow, model()->rowCount() - 1);
    if (row >= 0)
        setCurrentIndex(model()->index(row, 0));
    synchronizeBreakpoints();
}

bool BreakWindow::editBreakpoint(BreakpointData *data, QWidget *parent)
{
    BreakpointDialog dialog(parent);
    return dialog.showDialog(data);
}

void BreakWindow::addBreakpoint()
{
    BreakpointData *data = new BreakpointData();
    if (editBreakpoint(data, this))
        appendBreakpoint(data);
    else
        delete data;
}

void BreakWindow::editBreakpoints(const QModelIndexList &list)
{
    QTC_ASSERT(!list.isEmpty(), return);

    if (list.size() == 1) {
        BreakpointData *data = breakpointAt(0); 
        QTC_ASSERT(data, return);
        if (editBreakpoint(data, this))
            breakHandler()->reinsertBreakpoint(data);
        return;
    }

    // This allows to change properties of multiple breakpoints at a time.
    QDialog dlg(this);
    Ui::BreakCondition ui;
    ui.setupUi(&dlg);
    dlg.setWindowTitle(tr("Edit Breakpoint Properties"));
    ui.lineEditIgnoreCount->setValidator(
        new QIntValidator(0, 2147483647, ui.lineEditIgnoreCount));

    const QModelIndex idx = list.front();
    BreakpointData *data = breakpointAt(idx.row()); 
    QTC_ASSERT(data, return);

    const QString oldCondition = QString::fromLatin1(data->condition);
    const QString oldIgnoreCount = QString::number(data->ignoreCount);
    const QString oldThreadSpec = QString::fromLatin1(data->threadSpec);

    ui.lineEditCondition->setText(oldCondition);
    ui.lineEditIgnoreCount->setText(oldIgnoreCount);
    ui.lineEditThreadSpec->setText(oldThreadSpec);

    if (dlg.exec() == QDialog::Rejected)
        return;

    const QString newCondition = ui.lineEditCondition->text();
    const QString newIgnoreCount = ui.lineEditIgnoreCount->text();
    const QString newThreadSpec = ui.lineEditThreadSpec->text();

    // Unchanged -> cancel
    if (newCondition == oldCondition && newIgnoreCount == oldIgnoreCount
            && newThreadSpec == oldThreadSpec)
        return;

    foreach (const QModelIndex &idx, list) {
        BreakpointData *data = breakpointAt(idx.row()); 
        QTC_ASSERT(data, continue);
        data->condition = newCondition.toLatin1();
        data->ignoreCount = newIgnoreCount.toInt();
        data->threadSpec = newThreadSpec.toLatin1();
    }
    synchronizeBreakpoints();
}

void BreakWindow::associateBreakpoint(const QModelIndexList &list, int threadId)
{
    QByteArray condition;
    if (threadId != -1)
        condition = QByteArray::number(threadId);
    foreach (const QModelIndex &index, list) {
        BreakpointData *data = breakpointAt(index.row()); 
        QTC_ASSERT(data, continue);
        data->condition = condition;
    }
    synchronizeBreakpoints();
}

void BreakWindow::resizeColumnsToContents()
{
    for (int i = model()->columnCount(); --i >= 0; )
        resizeColumnToContents(i);
}

void BreakWindow::setAlwaysResizeColumnsToContents(bool on)
{
    m_alwaysResizeColumnsToContents = on;
    QHeaderView::ResizeMode mode = on
        ? QHeaderView::ResizeToContents : QHeaderView::Interactive;
    for (int i = model()->columnCount(); --i >= 0; )
        header()->setResizeMode(i, mode);
}

void BreakWindow::rowActivated(const QModelIndex &index)
{
    BreakpointData *data = breakpointAt(index.row());
    QTC_ASSERT(data, return);
    plugin()->gotoLocation(data->markerFileName(),
        data->markerLineNumber(), false);
}

} // namespace Internal
} // namespace Debugger

#include "breakwindow.moc"
