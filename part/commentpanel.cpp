/*
    SPDX-FileCopyrightText: 2026 Okular developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "commentpanel.h"

#include <algorithm>

#include <QCheckBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QLocale>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

#include <KLocalizedString>

#include "core/annotations.h"
#include "core/area.h"
#include "core/document.h"
#include "core/page.h"
#include "core/textpage.h"
#include "settings.h"

namespace
{
constexpr int CommentPanelMinimumWidth = 300;

QString formattedDate(const QDateTime &dateTime)
{
    if (!dateTime.isValid()) {
        return QString();
    }
    return QLocale().toString(dateTime.toLocalTime(), QLocale::ShortFormat);
}

QString authorName(const Okular::Annotation *annotation)
{
    const QString author = annotation->author().trimmed();
    return author.isEmpty() ? i18nc("@info unknown author of a comment", "Unknown author") : author;
}

/**
 * Text marked by the comment. It is stored in the annotation window summary when the
 * comment is created, so it can be shown even before the page text has been generated.
 */
QString quoteForComment(Okular::Document *document, const Okular::Page *page, const Okular::Annotation *annotation)
{
    const QString stored = annotation->window().summary().simplified();
    if (!stored.isEmpty()) {
        return stored;
    }

    const auto *highlight = dynamic_cast<const Okular::HighlightAnnotation *>(annotation);
    if (!highlight || highlight->highlightQuads().isEmpty()) {
        return QString();
    }
    if (!page->hasTextPage()) {
        document->requestTextPage(page->number());
        return QString();
    }

    Okular::RegularAreaRect area;
    for (const Okular::HighlightAnnotation::Quad &quad : highlight->highlightQuads()) {
        const Okular::NormalizedPoint bottomLeft = quad.point(0);
        const Okular::NormalizedPoint topRight = quad.point(2);
        area.append(Okular::NormalizedRect(bottomLeft.x, topRight.y, topRight.x, bottomLeft.y));
    }

    QString text = page->text(&area, Okular::TextPage::CentralPixelTextAreaInclusionBehaviour);
    text = text.replace(QStringLiteral("-\n"), QString());
    text = text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.simplified();
}
}

/**
 * @short A single comment shown as a card in the comment panel.
 *
 * The card never changes the document itself; it asks the panel to do so, so the
 * panel can rebuild the card list without deleting a widget from within its own code.
 */
class CommentCard : public QFrame
{
    Q_OBJECT

public:
    CommentCard(Okular::Document *document, int pageNumber, Okular::Annotation *annotation, const QString &quote, QWidget *parent);

    Okular::Annotation *annotation() const;
    int pageNumber() const;

    void beginEditing();
    void endEditing();

Q_SIGNALS:
    void navigateRequested(int pageNumber);
    void saveRequested(int pageNumber, Okular::Annotation *annotation, const QString &contents);
    void resolvedRequested(int pageNumber, Okular::Annotation *annotation, bool resolved);
    void deleteRequested(int pageNumber, Okular::Annotation *annotation);
    void editingStarted();
    void editingEnded();

private:
    void updateStateUi();
    void updateEditorUi();

    Okular::Document *m_document;
    Okular::Annotation *m_annotation;
    int m_pageNumber;

    QLabel *m_authorLabel;
    QLabel *m_dateLabel;
    QLabel *m_stateLabel;
    QLabel *m_quoteLabel;
    QLabel *m_contentsLabel;
    QPlainTextEdit *m_editor;
    QWidget *m_editorButtons;
    QToolButton *m_editButton;
    QToolButton *m_resolveButton;
    QToolButton *m_deleteButton;

    QFont m_normalContentsFont;
    QPalette m_normalContentsPalette;
    bool m_editing = false;
    bool m_hadContents = false;
};

CommentCard::CommentCard(Okular::Document *document, int pageNumber, Okular::Annotation *annotation, const QString &quote, QWidget *parent)
    : QFrame(parent)
    , m_document(document)
    , m_annotation(annotation)
    , m_pageNumber(pageNumber)
{
    setObjectName(QStringLiteral("commentCard"));
    setStyleSheet(QStringLiteral("QFrame#commentCard { background-color: palette(base); border: 1px solid palette(mid); border-radius: 6px; }"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    // Header: who wrote the comment and when
    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    m_authorLabel = new QLabel(this);
    QFont authorFont = m_authorLabel->font();
    authorFont.setBold(true);
    m_authorLabel->setFont(authorFont);
    m_authorLabel->setText(authorName(annotation));
    m_authorLabel->setTextFormat(Qt::PlainText);
    m_authorLabel->setToolTip(annotation->author());

    m_dateLabel = new QLabel(this);
    m_dateLabel->setText(formattedDate(annotation->creationDate()));
    m_dateLabel->setTextFormat(Qt::PlainText);
    m_dateLabel->setToolTip(QLocale().toString(annotation->creationDate().toLocalTime(), QLocale::LongFormat));
    QFont dateFont = m_dateLabel->font();
    dateFont.setPointSizeF(dateFont.pointSizeF() * 0.9);
    m_dateLabel->setFont(dateFont);

    m_stateLabel = new QLabel(this);
    QFont stateFont = m_stateLabel->font();
    stateFont.setPointSizeF(stateFont.pointSizeF() * 0.9);
    stateFont.setItalic(true);
    m_stateLabel->setFont(stateFont);

    headerLayout->addWidget(m_authorLabel);
    headerLayout->addWidget(m_stateLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_dateLabel);
    layout->addLayout(headerLayout);

    // The comment itself, above the passage it is attached to
    m_contentsLabel = new QLabel(this);
    m_contentsLabel->setText(annotation->contents().isEmpty() ? i18nc("@info a comment that has no text yet", "Write a comment…") : annotation->contents());
    m_contentsLabel->setTextFormat(Qt::PlainText);
    m_contentsLabel->setWordWrap(true);
    m_contentsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_contentsLabel);

    m_normalContentsFont = m_contentsLabel->font();
    m_normalContentsPalette = m_contentsLabel->palette();
    m_hadContents = !annotation->contents().isEmpty();

    // The text the comment is attached to, kept small so it does not compete with the comment
    m_quoteLabel = new QLabel(this);
    QString displayQuote = quote;
    if (displayQuote.length() > 220) {
        displayQuote = displayQuote.left(220) + QChar(0x2026);
    }
    m_quoteLabel->setText(displayQuote);
    m_quoteLabel->setTextFormat(Qt::PlainText);
    m_quoteLabel->setWordWrap(true);
    m_quoteLabel->setVisible(!displayQuote.isEmpty());
    m_quoteLabel->setStyleSheet(QStringLiteral("QLabel { border-left: 2px solid palette(mid); padding-left: 6px; }"));
    QFont quoteFont = m_quoteLabel->font();
    quoteFont.setItalic(true);
    quoteFont.setPointSizeF(quoteFont.pointSizeF() * 0.9);
    m_quoteLabel->setFont(quoteFont);
    m_quoteLabel->setMaximumHeight(m_quoteLabel->fontMetrics().lineSpacing() * 3);
    layout->addWidget(m_quoteLabel);

    m_editor = new QPlainTextEdit(this);
    m_editor->setPlainText(annotation->contents());
    m_editor->setPlaceholderText(i18nc("@info placeholder in the comment editor", "Write a comment…"));
    m_editor->setTabChangesFocus(true);
    m_editor->setMinimumHeight(m_editor->fontMetrics().lineSpacing() * 3);
    m_editor->setVisible(false);
    layout->addWidget(m_editor);

    m_editorButtons = new QWidget(this);
    auto *editorButtonsLayout = new QHBoxLayout(m_editorButtons);
    editorButtonsLayout->setContentsMargins(0, 0, 0, 0);
    editorButtonsLayout->setSpacing(6);
    editorButtonsLayout->addStretch();
    auto *saveButton = new QToolButton(m_editorButtons);
    saveButton->setText(i18nc("@action:button save the edited comment", "Save"));
    saveButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")));
    saveButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *cancelButton = new QToolButton(m_editorButtons);
    cancelButton->setText(i18nc("@action:button discard the edited comment", "Cancel"));
    cancelButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-cancel")));
    cancelButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    editorButtonsLayout->addWidget(saveButton);
    editorButtonsLayout->addWidget(cancelButton);
    m_editorButtons->setVisible(false);
    layout->addWidget(m_editorButtons);

    connect(saveButton, &QToolButton::clicked, this, [this] {
        const QString contents = m_editor->toPlainText().trimmed();
        endEditing();
        if (!contents.isEmpty() && contents != m_annotation->contents()) {
            Q_EMIT saveRequested(m_pageNumber, m_annotation, contents);
        } else if (contents.isEmpty() && !m_hadContents) {
            // The comment was created but never written: do not keep an empty card
            Q_EMIT deleteRequested(m_pageNumber, m_annotation);
        }
    });
    connect(cancelButton, &QToolButton::clicked, this, [this] {
        const QString contents = m_editor->toPlainText().trimmed();
        const bool deleteEmptyComment = contents.isEmpty() && !m_hadContents;
        m_editor->setPlainText(m_annotation->contents());
        endEditing();
        if (deleteEmptyComment) {
            Q_EMIT deleteRequested(m_pageNumber, m_annotation);
        }
    });

    // Actions on the card
    auto *actionsLayout = new QHBoxLayout;
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(2);

    auto *pageButton = new QToolButton(this);
    pageButton->setText(i18nc("@action:button go to the page of the comment", "Page %1", pageNumber + 1));
    pageButton->setIcon(QIcon::fromTheme(QStringLiteral("go-jump")));
    pageButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pageButton->setAutoRaise(true);
    connect(pageButton, &QToolButton::clicked, this, [this] { Q_EMIT navigateRequested(m_pageNumber); });
    actionsLayout->addWidget(pageButton);
    actionsLayout->addStretch();

    m_editButton = new QToolButton(this);
    m_editButton->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));
    m_editButton->setToolTip(i18nc("@info:tooltip edit the comment", "Edit comment"));
    m_editButton->setAutoRaise(true);
    m_editButton->setEnabled(m_document->canModifyPageAnnotation(annotation));
    connect(m_editButton, &QToolButton::clicked, this, &CommentCard::beginEditing);
    actionsLayout->addWidget(m_editButton);

    m_resolveButton = new QToolButton(this);
    m_resolveButton->setAutoRaise(true);
    m_resolveButton->setEnabled(m_document->canModifyPageAnnotation(annotation));
    connect(m_resolveButton, &QToolButton::clicked, this, [this] {
        const bool resolve = m_annotation->commentState() == Okular::Annotation::OpenComment;
        Q_EMIT resolvedRequested(m_pageNumber, m_annotation, resolve);
    });
    actionsLayout->addWidget(m_resolveButton);

    m_deleteButton = new QToolButton(this);
    m_deleteButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    m_deleteButton->setToolTip(i18nc("@info:tooltip delete the comment", "Delete comment"));
    m_deleteButton->setAutoRaise(true);
    m_deleteButton->setEnabled(m_document->canRemovePageAnnotation(annotation));
    connect(m_deleteButton, &QToolButton::clicked, this, [this] { Q_EMIT deleteRequested(m_pageNumber, m_annotation); });
    actionsLayout->addWidget(m_deleteButton);

    layout->addLayout(actionsLayout);

    updateStateUi();
}

Okular::Annotation *CommentCard::annotation() const
{
    return m_annotation;
}

int CommentCard::pageNumber() const
{
    return m_pageNumber;
}

void CommentCard::beginEditing()
{
    if (m_editing) {
        return;
    }
    m_editing = true;
    m_editor->setPlainText(m_annotation->contents());
    updateEditorUi();
    m_editor->setFocus();
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_editor->setTextCursor(cursor);
    Q_EMIT editingStarted();
}

void CommentCard::endEditing()
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    updateEditorUi();
    Q_EMIT editingEnded();
}

void CommentCard::updateEditorUi()
{
    m_contentsLabel->setVisible(!m_editing);
    m_editor->setVisible(m_editing);
    m_editorButtons->setVisible(m_editing);
    m_editButton->setVisible(!m_editing);
    // Resolving or deleting while writing is confusing, so those wait until the editor is closed
    m_resolveButton->setVisible(!m_editing);
    m_deleteButton->setVisible(!m_editing);
    m_quoteLabel->setVisible(!m_quoteLabel->text().isEmpty());
}

void CommentCard::updateStateUi()
{
    const bool resolved = m_annotation->commentState() == Okular::Annotation::ResolvedComment;

    if (resolved) {
        m_stateLabel->setText(i18nc("@info comment has been resolved", "Resolved"));
        m_dateLabel->setText(formattedDate(m_annotation->modificationDate()));
        m_dateLabel->setToolTip(QLocale().toString(m_annotation->modificationDate().toLocalTime(), QLocale::LongFormat));
        m_resolveButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
        m_resolveButton->setToolTip(i18nc("@info:tooltip reopen a resolved comment", "Reopen comment"));
        m_resolveButton->setText(i18nc("@action:button reopen a resolved comment", "Reopen"));
        QFont font = m_normalContentsFont;
        font.setStrikeOut(true);
        m_contentsLabel->setFont(font);
        QPalette palette = m_normalContentsPalette;
        palette.setColor(QPalette::WindowText, palette.color(QPalette::Disabled, QPalette::WindowText));
        m_contentsLabel->setPalette(palette);
    } else {
        m_stateLabel->clear();
        m_dateLabel->setText(formattedDate(m_annotation->creationDate()));
        m_dateLabel->setToolTip(QLocale().toString(m_annotation->creationDate().toLocalTime(), QLocale::LongFormat));
        m_resolveButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")));
        m_resolveButton->setToolTip(i18nc("@info:tooltip mark the comment as resolved", "Resolve comment"));
        m_resolveButton->setText(i18nc("@action:button mark the comment as resolved", "Resolve"));
        m_contentsLabel->setFont(m_normalContentsFont);
        m_contentsLabel->setPalette(m_normalContentsPalette);
    }
}

CommentPanel::CommentPanel(QWidget *parent, Okular::Document *document)
    : QWidget(parent)
    , m_document(document)
{
    setObjectName(QStringLiteral("commentPanel"));
    setMinimumWidth(CommentPanelMinimumWidth);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setAutoFillBackground(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto *titleLabel = new QLabel(i18nc("@title:tab document comments", "Comments"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.1);
    titleLabel->setFont(titleFont);
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    layout->addLayout(titleRow);

    m_summaryLabel = new QLabel(this);
    QFont summaryFont = m_summaryLabel->font();
    summaryFont.setPointSizeF(summaryFont.pointSizeF() * 0.9);
    m_summaryLabel->setFont(summaryFont);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    m_showResolvedCheckBox = new QCheckBox(i18nc("@option:check show comments that were resolved", "Show resolved comments"), this);
    connect(m_showResolvedCheckBox, &QCheckBox::toggled, this, &CommentPanel::reload);
    layout->addWidget(m_showResolvedCheckBox);

    m_emptyLabel = new QLabel(i18nc("@info empty comment panel", "Select text in the document, then right-click it and choose <b>Add Comment</b> to start a comment."), this);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    layout->addWidget(m_emptyLabel);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    auto *cardsContainer = new QWidget(m_scrollArea);
    m_cardsLayout = new QVBoxLayout(cardsContainer);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(8);
    m_cardsLayout->addStretch();
    m_scrollArea->setWidget(cardsContainer);
    layout->addWidget(m_scrollArea, 1);

    m_document->addObserver(this);
    reload();
}

CommentPanel::~CommentPanel()
{
    m_document->removeObserver(this);
}

void CommentPanel::notifySetup(const QList<Okular::Page *> &pages, int setupFlags)
{
    Q_UNUSED(pages)
    Q_UNUSED(setupFlags)
    // A new document invalidates every card, so a running edit cannot be kept
    m_editing = false;
    m_reloadPending = false;
    reload();
}

void CommentPanel::notifyPageChanged(int page, int flags)
{
    Q_UNUSED(page)
    if (flags & Okular::DocumentObserver::Annotations) {
        reload();
    }
}

void CommentPanel::notifyContentsCleared(int flags)
{
    if (flags & Okular::DocumentObserver::Annotations) {
        reload();
    }
}

void CommentPanel::setShowResolved(bool show)
{
    m_showResolvedCheckBox->setChecked(show);
}

bool CommentPanel::showResolved() const
{
    return m_showResolvedCheckBox->isChecked();
}

void CommentPanel::reload()
{
    if (m_editing) {
        m_reloadPending = true;
        return;
    }
    m_reloadPending = false;
    loadComments();
}

void CommentPanel::loadComments()
{
    // Remember where the view was so reloading does not jump around
    const int scrollValue = m_scrollArea->verticalScrollBar()->value();

    while (m_cardsLayout->count() > 1) {
        QLayoutItem *item = m_cardsLayout->takeAt(0);
        delete item->widget();
        delete item;
    }
    m_cards.clear();

    int openComments = 0;
    int resolvedComments = 0;
    const bool showResolvedComments = showResolved();

    for (uint pageIndex = 0; pageIndex < m_document->pages(); ++pageIndex) {
        const int pageNumber = static_cast<int>(pageIndex);
        const Okular::Page *page = m_document->page(pageNumber);
        const QList<Okular::Annotation *> annotations = page->annotations();

        QList<Okular::Annotation *> pageComments;
        for (Okular::Annotation *annotation : annotations) {
            switch (annotation->commentState()) {
            case Okular::Annotation::OpenComment:
                ++openComments;
                break;
            case Okular::Annotation::ResolvedComment:
                ++resolvedComments;
                break;
            case Okular::Annotation::NotAComment:
                continue;
            }
            pageComments.append(annotation);
        }

        std::sort(pageComments.begin(), pageComments.end(), [](const Okular::Annotation *left, const Okular::Annotation *right) { return left->creationDate() < right->creationDate(); });

        for (Okular::Annotation *annotation : std::as_const(pageComments)) {
            const bool resolved = annotation->commentState() == Okular::Annotation::ResolvedComment;
            if (resolved && !showResolvedComments) {
                continue;
            }

            auto *card = new CommentCard(m_document, pageNumber, annotation, quoteForComment(m_document, page, annotation), this);
            connect(card, &CommentCard::navigateRequested, this, &CommentPanel::navigateToPage);
            connect(card, &CommentCard::saveRequested, this, [this](int pageNumber, Okular::Annotation *annotation, const QString &contents) {
                QTimer::singleShot(0, this, [this, pageNumber, annotation, contents] {
                    m_document->prepareToModifyAnnotationProperties(annotation);
                    annotation->setContents(contents);
                    annotation->setModificationDate(QDateTime::currentDateTime());
                    m_document->modifyPageAnnotationProperties(pageNumber, annotation);
                });
            });
            connect(card, &CommentCard::resolvedRequested, this, [this](int pageNumber, Okular::Annotation *annotation, bool resolved) {
                QTimer::singleShot(0, this, [this, pageNumber, annotation, resolved] {
                    m_document->prepareToModifyAnnotationProperties(annotation);
                    annotation->setCommentState(resolved ? Okular::Annotation::ResolvedComment : Okular::Annotation::OpenComment);
                    annotation->setModificationDate(QDateTime::currentDateTime());
                    m_document->modifyPageAnnotationProperties(pageNumber, annotation);
                });
            });
            connect(card, &CommentCard::deleteRequested, this, [this](int pageNumber, Okular::Annotation *annotation) {
                QTimer::singleShot(0, this, [this, pageNumber, annotation] { m_document->removePageAnnotation(pageNumber, annotation); });
            });
            connect(card, &CommentCard::editingStarted, this, [this] { m_editing = true; });
            connect(card, &CommentCard::editingEnded, this, [this] {
                m_editing = false;
                if (m_reloadPending) {
                    reload();
                }
            });

            m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
            m_cards.append(card);
        }
    }

    m_emptyLabel->setVisible(m_cards.isEmpty());
    m_scrollArea->setVisible(!m_cards.isEmpty());
    m_showResolvedCheckBox->setVisible(resolvedComments > 0 || showResolvedComments);
    if (resolvedComments > 0) {
        m_showResolvedCheckBox->setText(i18ncp("@option:check", "Show %1 resolved comment", "Show %1 resolved comments", resolvedComments));
    } else {
        m_showResolvedCheckBox->setText(i18nc("@option:check", "Show resolved comments"));
    }
    updateSummary(openComments, resolvedComments);

    m_scrollArea->verticalScrollBar()->setValue(scrollValue);
}

void CommentPanel::updateSummary(int openComments, int resolvedComments)
{
    if (openComments == 0 && resolvedComments == 0) {
        m_summaryLabel->setText(i18nc("@info summary of the comment panel", "No comments yet"));
    } else if (openComments == 0) {
        m_summaryLabel->setText(i18ncp("@info summary of the comment panel", "%1 comment, all resolved", "%1 comments, all resolved", resolvedComments));
    } else if (resolvedComments == 0) {
        m_summaryLabel->setText(i18ncp("@info summary of the comment panel", "%1 open comment", "%1 open comments", openComments));
    } else {
        m_summaryLabel->setText(i18nc("@info summary of the comment panel", "%1 open, %2 resolved", openComments, resolvedComments));
    }

    Q_EMIT commentCountChanged(openComments, resolvedComments);
}

void CommentPanel::addCommentForSelection(int pageNumber)
{
    const Okular::Page *page = m_document->page(pageNumber);
    if (!page || !page->textSelection() || page->textSelection()->isEmpty()) {
        return;
    }
    if (!m_document->isAllowed(Okular::AllowNotes)) {
        return;
    }

    const Okular::RegularAreaRect selection = *page->textSelection();

    QString selectedText = page->text(&selection, Okular::TextPage::CentralPixelTextAreaInclusionBehaviour);
    selectedText = selectedText.replace(QStringLiteral("-\n"), QString());
    selectedText = selectedText.replace(QLatin1Char('\n'), QLatin1Char(' '));
    selectedText = selectedText.simplified();

    auto *annotation = new Okular::HighlightAnnotation();
    annotation->setHighlightType(Okular::HighlightAnnotation::Highlight);
    annotation->setContents(QString());
    // Comments are marked with a light, translucent yellow. Without an explicit colour the
    // highlight ends up opaque black once the PDF backend takes over drawing it.
    annotation->style().setColor(QColor(255, 226, 92));
    annotation->style().setOpacity(0.4);
    annotation->setCommentState(Okular::Annotation::OpenComment);
    annotation->setAuthor(Okular::Settings::identityAuthor());
    annotation->setCreationDate(QDateTime::currentDateTime());
    annotation->setModificationDate(annotation->creationDate());
    annotation->window().setSummary(selectedText);

    Okular::NormalizedRect boundary;
    bool firstQuad = true;
    for (const Okular::NormalizedRect &rect : selection) {
        Okular::HighlightAnnotation::Quad quad;
        quad.setCapStart(false);
        quad.setCapEnd(false);
        quad.setFeather(1.0);
        quad.setPoint(Okular::NormalizedPoint(rect.left, rect.bottom), 0);
        quad.setPoint(Okular::NormalizedPoint(rect.right, rect.bottom), 1);
        quad.setPoint(Okular::NormalizedPoint(rect.right, rect.top), 2);
        quad.setPoint(Okular::NormalizedPoint(rect.left, rect.top), 3);
        annotation->highlightQuads().append(quad);

        if (firstQuad) {
            boundary = rect;
            firstQuad = false;
        } else {
            boundary = boundary | rect;
        }
    }
    annotation->setBoundingRectangle(boundary);

    m_document->addPageAnnotation(pageNumber, annotation);

    // The highlight marks the passage now, so drop the selection overlay that would hide it
    m_document->setPageTextSelection(pageNumber, nullptr, QColor());

    // The document change rebuilt the cards, so look the new one up and start editing it
    for (CommentCard *card : std::as_const(m_cards)) {
        if (card->annotation() == annotation) {
            card->beginEditing();
            break;
        }
    }
}

#include "commentpanel.moc"
