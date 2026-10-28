/*
    SPDX-FileCopyrightText: 2026 Okular developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// clazy:excludeall=qstring-allocations

#include <QAction>
#include <QApplication>
#include <QDomDocument>
#include <QDomElement>
#include <QFrame>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTest>
#include <QTestEventList>
#include <QTimer>
#include <QToolButton>

#include "../core/annotations.h"
#include "../core/area.h"
#include "../core/document.h"
#include "../core/global.h"
#include "../core/page.h"
#include "../core/textpage.h"
#include "../part/commentpanel.h"
#include "../part/pageview.h"
#include "../part/part.h"
#include "../settings.h"

#include <memory>

namespace Okular
{
class CommentPanelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testCommentLifecycle();
    void testContextMenuOffersAddComment();
    void testAreaSelectionDragOffersAddComment();
};

static int commentCardCount(CommentPanel *panel)
{
    return panel->findChildren<QFrame *>(QStringLiteral("commentCard")).count();
}

void CommentPanelTest::testCommentLifecycle()
{
    Okular::Settings::instance(QStringLiteral("okularpartrc"));
    Okular::Settings::self()->setDefaults();
    Okular::Settings::setIdentityAuthor(QStringLiteral("Test Author"));

    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(part.openDocument(QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/file2.pdf")), 1));

    auto *document = part.m_document;
    QVERIFY(document);
    QVERIFY(document->isOpened());

    // Select a word of the page, the way a user does before commenting
    document->requestTextPage(0);
    QTRY_VERIFY(document->page(0)->hasTextPage());
    std::unique_ptr<Okular::RegularAreaRect> selection(document->page(0)->findText(0, QStringLiteral("Hola"), Okular::FromTop, Qt::CaseSensitive));
    QVERIFY(selection && !selection->isEmpty());
    document->setPageTextSelection(0, std::move(selection), QColor());
    QVERIFY(document->page(0)->textSelection());

    auto *panel = part.widget()->findChild<CommentPanel *>();
    QVERIFY(panel);
    panel->setVisible(true);

    // Creating a comment from the selection anchors a highlight and opens an editor
    panel->addCommentForSelection(0);
    QCOMPARE(document->page(0)->annotations().count(), 1);
    Okular::Annotation *comment = document->page(0)->annotations().constFirst();
    QCOMPARE(comment->commentState(), Okular::Annotation::OpenComment);
    QCOMPARE(comment->author(), QStringLiteral("Test Author"));
    QVERIFY(comment->creationDate().isValid());
    QCOMPARE(comment->window().summary(), QStringLiteral("Hola"));
    // Light, translucent yellow, otherwise the PDF backend draws the highlight opaque black
    QCOMPARE(comment->style().color(), QColor(255, 226, 92));
    QVERIFY(qFuzzyCompare(comment->style().opacity(), 0.4));

    const auto *highlight = dynamic_cast<const Okular::HighlightAnnotation *>(comment);
    QVERIFY(highlight);
    QVERIFY(!highlight->highlightQuads().isEmpty());
    QCOMPARE(commentCardCount(panel), 1);
    QVERIFY(panel->findChild<QPlainTextEdit *>());

    // Writing the comment in the card updates the annotation and its dates
    auto *editor = panel->findChild<QPlainTextEdit *>();
    QVERIFY(editor);
    QVERIFY(editor->isVisibleTo(panel));
    editor->setPlainText(QStringLiteral("This greeting needs a translation"));

    QToolButton *saveButton = nullptr;
    const QList<QToolButton *> buttons = panel->findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        if (button->text() == QStringLiteral("Save")) {
            saveButton = button;
            break;
        }
    }
    QVERIFY(saveButton);
    saveButton->click();
    QTRY_COMPARE(comment->contents(), QStringLiteral("This greeting needs a translation"));
    QVERIFY(comment->modificationDate() >= comment->creationDate());
    QTRY_VERIFY(!panel->findChild<QPlainTextEdit *>()->isVisibleTo(panel));
    QCOMPARE(commentCardCount(panel), 1);

    // The comment and its state survive saving and loading the document
    QDomDocument xml;
    QDomElement root = xml.createElement(QStringLiteral("annotation"));
    xml.appendChild(root);
    Okular::AnnotationUtils::storeAnnotation(comment, root, xml);
    std::unique_ptr<Okular::Annotation> restored(Okular::AnnotationUtils::createAnnotation(root));
    QVERIFY(restored);
    QCOMPARE(restored->commentState(), Okular::Annotation::OpenComment);
    QCOMPARE(restored->contents(), QStringLiteral("This greeting needs a translation"));
    QCOMPARE(restored->author(), QStringLiteral("Test Author"));
    QVERIFY(restored->creationDate().isValid());

    // Resolving hides the card but keeps the comment, so it can be recalled later
    document->prepareToModifyAnnotationProperties(comment);
    comment->setCommentState(Okular::Annotation::ResolvedComment);
    comment->setModificationDate(QDateTime::currentDateTime());
    document->modifyPageAnnotationProperties(0, comment);
    QCOMPARE(comment->commentState(), Okular::Annotation::ResolvedComment);
    QCOMPARE(document->page(0)->annotations().count(), 1);
    QTRY_COMPARE(commentCardCount(panel), 0);

    panel->setShowResolved(true);
    QVERIFY(panel->showResolved());
    QCOMPARE(commentCardCount(panel), 1);

    // Deleting removes it for good
    document->removePageAnnotation(0, comment);
    QCOMPARE(document->page(0)->annotations().count(), 0);
    QCOMPARE(commentCardCount(panel), 0);
}

void CommentPanelTest::testContextMenuOffersAddComment()
{
    Okular::Settings::instance(QStringLiteral("okularpartrc"));
    Okular::Settings::self()->setDefaults();

    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(part.openDocument(QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/file2.pdf")), 1));
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    auto *document = part.m_document;
    auto *pageView = part.m_pageView.data();
    document->setViewportPage(0);
    QTRY_VERIFY(document->page(0)->hasPixmap(pageView));
    QVERIFY(QMetaObject::invokeMethod(pageView, "slotSetMouseTextSelect"));

    const int width = pageView->horizontalScrollBar()->maximum() + pageView->viewport()->width();
    const int height = pageView->verticalScrollBar()->maximum() + pageView->viewport()->height();
    const int mouseY = height * 0.052;
    const int startX = width * 0.12;
    const int endX = width * 0.7;

    QTestEventList events;
    events.addMouseMove(QPoint(startX, mouseY));
    events.addMousePress(Qt::LeftButton, Qt::NoModifier, QPoint(startX, mouseY));
    events.addMouseMove(QPoint((startX + endX) / 2, mouseY));
    events.addDelay(50);
    events.addMouseMove(QPoint(endX, mouseY));
    events.addDelay(50);
    events.addMouseRelease(Qt::LeftButton, Qt::NoModifier, QPoint(endX, mouseY));
    events.simulate(pageView->viewport());

    QVERIFY(document->page(0)->textSelection());

    // Inspect the context menu while it is open, then close it
    QStringList actionTexts;
    QTimer probe;
    probe.setInterval(50);
    connect(&probe, &QTimer::timeout, this, [&actionTexts] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        const QList<QAction *> actions = menu->actions();
        for (const QAction *action : actions) {
            actionTexts << action->text();
        }
        menu->close();
    });
    probe.start();
    const QPoint clickPos((startX + endX) / 2, mouseY);
    QTest::mousePress(pageView->viewport(), Qt::RightButton, Qt::NoModifier, clickPos);
    QTest::mouseRelease(pageView->viewport(), Qt::RightButton, Qt::NoModifier, clickPos);
    probe.stop();

    QVERIFY2(!actionTexts.isEmpty(), "the text selection context menu did not open");
    QVERIFY2(actionTexts.join(QStringLiteral("|")).contains(QStringLiteral("Add Comment")), qPrintable(QStringLiteral("menu actions were: ") + actionTexts.join(QStringLiteral("|"))));
}

void CommentPanelTest::testAreaSelectionDragOffersAddComment()
{
    Okular::Settings::instance(QStringLiteral("okularpartrc"));
    Okular::Settings::self()->setDefaults();

    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(part.openDocument(QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/file2.pdf")), 1));
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    auto *document = part.m_document;
    auto *pageView = part.m_pageView.data();
    auto *panel = part.widget()->findChild<CommentPanel *>();
    QVERIFY(panel);
    document->setViewportPage(0);
    QTRY_VERIFY(document->page(0)->hasPixmap(pageView));
    // Area Selection tool: dragging a rectangle is the usual "select some text" gesture
    QVERIFY(QMetaObject::invokeMethod(pageView, "slotSetMouseSelect"));

    const int width = pageView->horizontalScrollBar()->maximum() + pageView->viewport()->width();
    const int height = pageView->verticalScrollBar()->maximum() + pageView->viewport()->height();
    const int mouseY = height * 0.052;
    const int startX = width * 0.12;
    const int endX = width * 0.7;

    QStringList actionTexts;
    QTimer probe;
    probe.setInterval(50);
    connect(&probe, &QTimer::timeout, this, [&actionTexts] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        QAction *addComment = nullptr;
        const QList<QAction *> actions = menu->actions();
        for (QAction *action : actions) {
            actionTexts << action->text();
            if (action->objectName() == QStringLiteral("AddComment")) {
                addComment = action;
            }
        }
        if (addComment) {
            // Clicking the entry the way a user does, so the menu returns the chosen action
            const QRect actionRect = menu->actionGeometry(addComment);
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, actionRect.center());
        } else {
            menu->close();
        }
    });

    QTestEventList events;
    events.addMouseMove(QPoint(startX, mouseY));
    events.addMousePress(Qt::LeftButton, Qt::NoModifier, QPoint(startX, mouseY));
    events.addMouseMove(QPoint((startX + endX) / 2, mouseY));
    events.addDelay(50);
    events.addMouseMove(QPoint(endX, mouseY));
    events.addDelay(50);
    events.addMouseRelease(Qt::LeftButton, Qt::NoModifier, QPoint(endX, mouseY));

    probe.start();
    events.simulate(pageView->viewport());
    probe.stop();

    QVERIFY2(actionTexts.join(QStringLiteral("|")).contains(QStringLiteral("Add Comment")), qPrintable(QStringLiteral("menu actions were: ") + actionTexts.join(QStringLiteral("|"))));
    QTRY_COMPARE(document->page(0)->annotations().count(), 1);
    Okular::Annotation *comment = document->page(0)->annotations().constFirst();
    QCOMPARE(comment->commentState(), Okular::Annotation::OpenComment);
    QTRY_COMPARE(commentCardCount(panel), 1);
}
}

QTEST_MAIN(Okular::CommentPanelTest)
#include "commentpaneltest.moc"
