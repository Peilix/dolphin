/***************************************************************************
 *   Copyright (C) 2008 by Peter Penz <peter.penz@gmx.at>                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA            *
 ***************************************************************************/

#include "selectionmanager.h"

#include "dolphinmodel.h"
#include "selectiontoggle.h"
#include <kdirmodel.h>
#include <kiconeffect.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QModelIndex>
#include <QPainter>
#include <QPaintEvent>
#include <QRect>
#include <QTimeLine>

SelectionManager::SelectionManager(QAbstractItemView* parent) :
    QObject(parent),
    m_view(parent),
    m_toggle(0),
    m_connected(false)
{
    connect(parent, SIGNAL(entered(const QModelIndex&)),
            this, SLOT(slotEntered(const QModelIndex&)));
    connect(parent, SIGNAL(viewportEntered()),
            this, SLOT(slotViewportEntered()));
    m_toggle = new SelectionToggle(m_view->viewport());
    m_toggle->setCheckable(true);
    m_toggle->hide();
    connect(m_toggle, SIGNAL(clicked(bool)),
            this, SLOT(setItemSelected(bool)));

    m_view->viewport()->installEventFilter(this);
}

SelectionManager::~SelectionManager()
{
}

bool SelectionManager::eventFilter(QObject* watched, QEvent* event)
{
    Q_ASSERT(watched == m_view->viewport());
    if (event->type() == QEvent::MouseButtonPress) {
        // Set the toggle invisible, if a mouse button has been pressed
        // outside the toggle boundaries. This e.g. assures, that the toggle
        // gets invisible during dragging items.
        const QRect toggleBounds(m_toggle->mapToGlobal(QPoint(0, 0)), m_toggle->size());
        m_toggle->setVisible(toggleBounds.contains(QCursor::pos()));
    }
    return QObject::eventFilter(watched, event);
}

void SelectionManager::reset()
{
    m_toggle->reset();
}

void SelectionManager::slotEntered(const QModelIndex& index)
{
    m_toggle->hide();
    const bool showToggle = index.isValid() &&
                            (index.column() == DolphinModel::Name) &&
                            (QApplication::mouseButtons() == Qt::NoButton);
    if (showToggle) {
        m_toggle->setUrl(urlForIndex(index));

        if (!m_connected) {
            connect(m_view->model(), SIGNAL(rowsRemoved(const QModelIndex&, int, int)),
                    this, SLOT(slotRowsRemoved(const QModelIndex&, int, int)));
            connect(m_view->selectionModel(),
                    SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)),
                    this,
                    SLOT(slotSelectionChanged(const QItemSelection&, const QItemSelection&)));
            m_connected = true;
        }

        // Increase the size of the toggle for large items
        const int iconHeight = m_view->iconSize().height();

        int toggleSize = KIconLoader::SizeSmall;
        if (iconHeight >= KIconLoader::SizeEnormous) {
            toggleSize = KIconLoader::SizeMedium;
        } else if (iconHeight >= KIconLoader::SizeLarge) {
            toggleSize = KIconLoader::SizeSmallMedium;
        }

        // Add a small invisible margin, if the item-height is nearly
        // equal to the toggleSize (#169494).
        const QRect rect = m_view->visualRect(index);
        int margin = (rect.height() - toggleSize) / 2;
        if (margin > 4) {
            margin = 0;
        }
        toggleSize += 2 * margin;
        m_toggle->setMargin(margin);
        m_toggle->resize(toggleSize, toggleSize);
        m_toggle->move(rect.topLeft());

        QItemSelectionModel* selModel = m_view->selectionModel();
        m_toggle->setChecked(selModel->isSelected(index));
        m_toggle->show();
    } else {
        m_toggle->setUrl(KUrl());
        disconnect(m_view->model(), SIGNAL(rowsRemoved(const QModelIndex&, int, int)),
                   this, SLOT(slotRowsRemoved(const QModelIndex&, int, int)));
        disconnect(m_view->selectionModel(),
                   SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)),
                   this,
                   SLOT(slotSelectionChanged(const QItemSelection&, const QItemSelection&)));
        m_connected = false;
    }
}

void SelectionManager::slotViewportEntered()
{
    m_toggle->hide();
}

void SelectionManager::setItemSelected(bool selected)
{
    emit selectionChanged();

    if (!m_toggle->url().isEmpty()) {
        const QModelIndex index = indexForUrl(m_toggle->url());
        if (index.isValid()) {
            QItemSelectionModel* selModel = m_view->selectionModel();
            if (selected) {
                selModel->select(index, QItemSelectionModel::Select);
            } else {
                selModel->select(index, QItemSelectionModel::Deselect);
            }
            selModel->setCurrentIndex(index, QItemSelectionModel::Current);
        }
    }
}

void SelectionManager::slotRowsRemoved(const QModelIndex& parent, int start, int end)
{
    Q_UNUSED(parent);
    Q_UNUSED(start);
    Q_UNUSED(end);
    m_toggle->hide();
}

void SelectionManager::slotSelectionChanged(const QItemSelection& selected,
                                            const QItemSelection& deselected)
{
    // The selection has been changed outside the scope of the selection manager
    // (e. g. by the rubberband or the "Select All" action). Take care updating
    // the state of the toggle button.
    if (!m_toggle->url().isEmpty()) {
        const QModelIndex index = indexForUrl(m_toggle->url());
        if (index.isValid()) {
            if (selected.contains(index)) {
                m_toggle->setChecked(true);
            }

            if (deselected.contains(index)) {
                m_toggle->setChecked(false);
            }
        }
    }
}

KUrl SelectionManager::urlForIndex(const QModelIndex& index) const
{
    QAbstractProxyModel* proxyModel = static_cast<QAbstractProxyModel*>(m_view->model());
    KDirModel* dirModel = static_cast<KDirModel*>(proxyModel->sourceModel());
    const QModelIndex dirIndex = proxyModel->mapToSource(index);
    return dirModel->itemForIndex(dirIndex).url();
}

const QModelIndex SelectionManager::indexForUrl(const KUrl& url) const
{
    QAbstractProxyModel* proxyModel = static_cast<QAbstractProxyModel*>(m_view->model());
    KDirModel* dirModel = static_cast<KDirModel*>(proxyModel->sourceModel());
    const QModelIndex dirIndex = dirModel->indexForUrl(url);
    return proxyModel->mapFromSource(dirIndex);
}

#include "selectionmanager.moc"