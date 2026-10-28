/*
    SPDX-FileCopyrightText: 2026 Okular developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef COMMENTPANEL_H
#define COMMENTPANEL_H

#include <QList>
#include <QWidget>

#include "core/observer.h"
#include "okularpart_export.h"

class QCheckBox;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace Okular
{
class Annotation;
class Document;
class Page;
}

class CommentCard;

/**
 * @short Panel listing the comments anchored in the document.
 *
 * Comments are stored as text highlight annotations that carry the comment
 * text as their contents, so they are saved with the document and keep the
 * author and the creation/modification date Okular already stores for every
 * annotation. Resolved comments are kept in the document as well, but are
 * hidden from the panel unless the user asks to see them again.
 */
class OKULARPART_EXPORT CommentPanel : public QWidget, public Okular::DocumentObserver
{
    Q_OBJECT

public:
    CommentPanel(QWidget *parent, Okular::Document *document);
    ~CommentPanel() override;

    // [INHERITED] from DocumentObserver
    void notifySetup(const QList<Okular::Page *> &pages, int setupFlags) override;
    void notifyPageChanged(int page, int flags) override;
    void notifyContentsCleared(int flags) override;

    /**
     * Creates a comment anchored at the text selected on @p pageNumber and
     * opens its editor. Requires a text selection on that page.
     */
    void addCommentForSelection(int pageNumber);

    void setShowResolved(bool show);
    bool showResolved() const;

Q_SIGNALS:
    /**
     * Emitted when the user asks to jump to the page a comment is anchored at.
     */
    void navigateToPage(int pageNumber);

    /**
     * Emitted whenever the number of open or resolved comments changes.
     */
    void commentCountChanged(int openComments, int resolvedComments);

private Q_SLOTS:
    void reload();

private:
    void startEditing(Okular::Annotation *annotation);
    void updateSummary(int openComments, int resolvedComments);
    void loadComments();

    Okular::Document *m_document;
    QVBoxLayout *m_cardsLayout;
    QLabel *m_summaryLabel;
    QLabel *m_emptyLabel;
    QCheckBox *m_showResolvedCheckBox;
    QScrollArea *m_scrollArea;
    QList<CommentCard *> m_cards;
    bool m_editing = false;
    bool m_reloadPending = false;
};

#endif // COMMENTPANEL_H
