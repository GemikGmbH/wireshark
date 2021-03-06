/* expert_info_dialog.cpp
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "expert_info_dialog.h"
#include <ui_expert_info_dialog.h>

#include "file.h"

#include <epan/epan_dissect.h>
#include <epan/expert.h>
#include <epan/stat_tap_ui.h>
#include <epan/tap.h>

#include "color_utils.h"
#include "wireshark_application.h"

#include <QAction>
#include <QHash>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidgetItemIterator>

// To do:
// - Test with custom expert levels (Preferences -> Protocols -> Expert).
//   - Figure out why the expert level prefs are buried under "Protocols".
// - Test with large captures. Add a custom model if needed.
// - Promote to a fourth pane in the main window?
// - Make colors configurable? In theory we could condense image/expert_indicators.svg,
//   down to one item, make sure it uses a single (or a few) base color(s), and generate
//   icons on the fly.

const int severity_col_ = 0;
const int group_col_    = 1;
const int protocol_col_ = 2;
const int count_col_    = 3;

const int packet_col_ = 0;

const int group_type_ = 1000;
const int packet_type_ = 1001;

const int auto_expand_threshold_ = 20; // Arbitrary

class ExpertGroupTreeWidgetItem : public QTreeWidgetItem
{
public:
    ExpertGroupTreeWidgetItem(QTreeWidget *parent, int severity, int group, const QString &protocol) : QTreeWidgetItem (parent, group_type_) {
        // XXX We set text and data here, colors in addExpertInfo, and counts
        // in updateCounts.
        setData(severity_col_, Qt::UserRole, QVariant(severity));
        setData(group_col_, Qt::UserRole, QVariant(group));

        setText(severity_col_, val_to_str_const(severity, expert_severity_vals, "Unknown"));
        setText(group_col_, val_to_str_const(group, expert_group_vals, "Unknown"));
        setText(protocol_col_, protocol);
        setText(count_col_, "0");
    }
    bool operator< (const QTreeWidgetItem &other) const
    {
        int sort_col = treeWidget()->sortColumn();
        switch(sort_col) {
            case severity_col_:
            return data(severity_col_, Qt::UserRole).value<int>() < other.data(severity_col_, Qt::UserRole).value<int>();
        case count_col_:
            return text(count_col_).toInt() < other.text(count_col_).toInt();
        default:
            return QTreeWidgetItem::operator<(other);
        }
    }
};

class ExpertPacketTreeWidgetItem : public QTreeWidgetItem
{
public:
    ExpertPacketTreeWidgetItem(expert_info_t *expert_info = NULL) :
        QTreeWidgetItem (packet_type_),
        packet_num_(0),
        hf_id_(-1)
    {
        if (expert_info) {
            packet_num_ = expert_info->packet_num;
            hf_id_ = expert_info->hf_index;
            protocol_ = expert_info->protocol;
            summary_ = expert_info->summary;
        }
        setFirstColumnSpanned(true);
        setText(packet_col_, QString("%1: %2")
                           .arg(packet_num_)
                           .arg(summary_));
    }
    guint32 packetNum() const { return packet_num_; }
    int hfId() const { return hf_id_; }
    QString protocol() const { return protocol_; }
    QString summary() const { return summary_; }
    bool operator< (const QTreeWidgetItem &other) const
    {
        // Probably not needed.
        if (other.type() != packet_type_) return QTreeWidgetItem::operator< (other);
        const ExpertPacketTreeWidgetItem *other_expert = static_cast<const ExpertPacketTreeWidgetItem *>(&other);
        // Force ascending.
        if (treeWidget()->header()->sortIndicatorOrder() == Qt::DescendingOrder) {
            return packet_num_ > other_expert->packetNum();
        } else {
            return packet_num_ < other_expert->packetNum();
        }
    }
private:
    guint32 packet_num_;
    int hf_id_;
    QString protocol_;
    QString summary_;
};

ExpertInfoDialog::ExpertInfoDialog(QWidget &parent, CaptureFile &capture_file) :
    WiresharkDialog(parent, capture_file),
    ui(new Ui::ExpertInfoDialog),
    need_show_hide_(false),
    display_filter_(QString())
{
    ui->setupUi(this);

    setWindowSubtitle(tr("Expert Information"));

    // Clicking on an item jumps to its associated packet. Make the dialog
    // narrow so that we avoid obscuring the packet list.
    // XXX Use recent settings instead
    int dlg_width = parent.width() * 3 / 5;
    if (dlg_width < width()) dlg_width = width();
    resize(dlg_width, parent.height());

    int one_em = fontMetrics().height();
    ui->expertInfoTreeWidget->setColumnWidth(severity_col_, one_em * 25); // Arbitrary

    severity_actions_ = QList<QAction *>() << ui->actionShowError << ui->actionShowWarning
                                           << ui->actionShowNote << ui->actionShowChat
                                           << ui->actionShowComment;
    QList<int> severities = QList<int>() << PI_ERROR << PI_WARN << PI_NOTE << PI_CHAT << PI_COMMENT;
    QMenu *severity_menu = new QMenu();

    // It might be nice to color each menu item to match each severity. It
    // might also be nice if Qt supported that...
    foreach (QAction *sa, severity_actions_) {
        severity_menu->addAction(sa);
        sa->setData(QVariant(severities.takeFirst()));
        sa->setChecked(true);
        connect(sa, SIGNAL(toggled(bool)), this, SLOT(actionShowToggled()));
    }
    ui->severitiesPushButton->setMenu(severity_menu);

    ui->expertInfoTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->expertInfoTreeWidget, SIGNAL(customContextMenuRequested(QPoint)),
                SLOT(showProtoHierMenu(QPoint)));

    QMenu *submenu;

    FilterAction::Action cur_action = FilterAction::ActionApply;
    submenu = ctx_menu_.addMenu(FilterAction::actionName(cur_action));
    foreach (FilterAction::ActionType at, FilterAction::actionTypes()) {
        FilterAction *fa = new FilterAction(submenu, cur_action, at);
        submenu->addAction(fa);
        connect(fa, SIGNAL(triggered()), this, SLOT(filterActionTriggered()));
    }

    cur_action = FilterAction::ActionPrepare;
    submenu = ctx_menu_.addMenu(FilterAction::actionName(cur_action));
    foreach (FilterAction::ActionType at, FilterAction::actionTypes()) {
        FilterAction *fa = new FilterAction(submenu, cur_action, at);
        submenu->addAction(fa);
        connect(fa, SIGNAL(triggered()), this, SLOT(filterActionTriggered()));
    }

    FilterAction *fa;
    QList<FilterAction::Action> extra_actions =
            QList<FilterAction::Action>() << FilterAction::ActionFind
                                          << FilterAction::ActionColorize
                                          << FilterAction::ActionWebLookup
                                          << FilterAction::ActionCopy;

    foreach (FilterAction::Action extra_action, extra_actions) {
        fa = new FilterAction(&ctx_menu_, extra_action);
        ctx_menu_.addAction(fa);
        connect(fa, SIGNAL(triggered()), this, SLOT(filterActionTriggered()));
    }

    setDisplayFilter();
    QTimer::singleShot(0, this, SLOT(retapPackets()));
}

ExpertInfoDialog::~ExpertInfoDialog()
{
    remove_tap_listener(this);
    delete ui;
}

void ExpertInfoDialog::clearAllData()
{
    ui->expertInfoTreeWidget->clear();
    error_events_ = 0;
    warn_events_ = 0;
    note_events_ = 0;
    chat_events_ = 0;
    comment_events_ = 0;

    need_show_hide_ = false;
    ei_to_ti_.clear();
    gti_packets_.clear();
}

void ExpertInfoDialog::setDisplayFilter(const QString &display_filter)
{
    display_filter_ = display_filter;
    updateWidgets();
}

void ExpertInfoDialog::retapPackets()
{
    if (file_closed_) return;

    clearAllData();
    remove_tap_listener(this);

    GString *error_string = register_tap_listener("expert",
                                                 this,
                                                 NULL,
                                                 TL_REQUIRES_NOTHING,
                                                 tapReset,
                                                 tapPacket,
                                                 tapDraw);
    if (error_string) {
        QMessageBox::warning(this, tr("Endpoint expert failed to register tap listener"),
                             error_string->str);
        g_string_free(error_string, TRUE);
        return;
    }

    if (ui->limitCheckBox->isChecked()) {
        error_string = set_tap_dfilter(this, display_filter_.toUtf8().constData());
        if (error_string) {
            QMessageBox::warning(this, tr("Endpoint expert failed to set filter"),
                                 error_string->str);
            g_string_free(error_string, TRUE);
            return;
        }
    }

    cap_file_.retapPackets();

    setUpdatesEnabled(false);
    // Adding a list of ExpertPacketTreeWidgetItems is much faster than
    // adding them individually. We still add ExpertGroupTreeWidgetItems
    // individually since that gives us a nice progress indicator.
    for (int i = 0; i < ui->expertInfoTreeWidget->topLevelItemCount(); i++) {
        QTreeWidgetItem *group_ti = ui->expertInfoTreeWidget->topLevelItem(i);
        if (gti_packets_.contains(group_ti)) {
            group_ti->addChildren(gti_packets_[group_ti]);
            if (group_ti->childCount() <= auto_expand_threshold_) {
                group_ti->setExpanded(true);
            }
        }
    }
    setUpdatesEnabled(true);

    updateWidgets();
}

void ExpertInfoDialog::addExpertInfo(struct expert_info_s *expert_info)
{
    if (!expert_info) return;
    QTreeWidgetItem *group_ti;

    QString key = QString("%1|%2|%3")
            .arg(expert_info->severity)
            .arg(expert_info->group)
            .arg(expert_info->protocol);

    QColor background;
    switch(expert_info->severity) {
    case(PI_COMMENT):
        background = ColorUtils::expert_color_comment;
        break;
    case(PI_CHAT):
        background = ColorUtils::expert_color_chat;
        break;
    case(PI_NOTE):
        background = ColorUtils::expert_color_note;
        break;
    case(PI_WARN):
        background = ColorUtils::expert_color_warn;
        break;
    case(PI_ERROR):
        background = ColorUtils::expert_color_error;
        break;
    default:
        break;
    }

    if (ei_to_ti_.contains(key)) {
        group_ti = ei_to_ti_[key];
    } else {
        group_ti = new ExpertGroupTreeWidgetItem(ui->expertInfoTreeWidget, expert_info->severity, expert_info->group, expert_info->protocol);
        if (background.isValid()) {
            for (int i = 0; i < ui->expertInfoTreeWidget->columnCount(); i++) {
                group_ti->setBackground(i, background);
                group_ti->setForeground(i, ColorUtils::expert_color_foreground);
            }
        }
        ei_to_ti_[key] = group_ti;
        gti_packets_[group_ti] = QList<QTreeWidgetItem *>();
    }

    gti_packets_[group_ti] << new ExpertPacketTreeWidgetItem(expert_info);

    // XXX Use plain colors until our users demand to be blinded.
//    if (background.isValid()) {
//        packet_ti->setBackground(0, background);
//        packet_ti->setForeground(0, ColorUtils::expert_color_foreground);
    //    }
}

void ExpertInfoDialog::updateCounts()
{
    for (int i = 0; i < ui->expertInfoTreeWidget->topLevelItemCount(); i++) {
        QTreeWidgetItem *group_ti = ui->expertInfoTreeWidget->topLevelItem(i);
        group_ti->setText(count_col_, QString::number(group_ti->childCount()));
    }
}

void ExpertInfoDialog::tapReset(void *eid_ptr)
{
    ExpertInfoDialog *eid = static_cast<ExpertInfoDialog *>(eid_ptr);
    if (!eid) return;

    eid->clearAllData();
}

gboolean ExpertInfoDialog::tapPacket(void *eid_ptr, struct _packet_info *pinfo, struct epan_dissect *, const void *data)
{
    ExpertInfoDialog *eid = static_cast<ExpertInfoDialog *>(eid_ptr);
    expert_info_t    *expert_info = (expert_info_t *) data;
    gboolean draw_required = FALSE;

    if (!pinfo || !eid || !expert_info) return FALSE;

    eid->addExpertInfo(expert_info);

    switch(expert_info->severity) {
    case(PI_COMMENT):
        if (eid->comment_events_ < 1) draw_required = TRUE;
        eid->comment_events_++;
        break;
    case(PI_CHAT):
        if (eid->chat_events_ < 1) draw_required = TRUE;
        eid->chat_events_++;
        break;
    case(PI_NOTE):
        if (eid->note_events_ < 1) draw_required = TRUE;
        eid->note_events_++;
        break;
    case(PI_WARN):
        if (eid->warn_events_ < 1) draw_required = TRUE;
        eid->warn_events_++;
        break;
    case(PI_ERROR):
        if (eid->error_events_ < 1) draw_required = TRUE;
        eid->error_events_++;
        break;
    default:
        g_assert_not_reached();
    }

    return draw_required;
}

void ExpertInfoDialog::tapDraw(void *eid_ptr)
{
    ExpertInfoDialog *eid = static_cast<ExpertInfoDialog *>(eid_ptr);
    if (!eid) return;

    eid->updateWidgets();
}

void ExpertInfoDialog::updateWidgets()
{
    ui->limitCheckBox->setEnabled(! file_closed_ && ! display_filter_.isEmpty());

    ui->actionShowError->setEnabled(error_events_ > 0);
    ui->actionShowWarning->setEnabled(warn_events_ > 0);
    ui->actionShowNote->setEnabled(note_events_ > 0);
    ui->actionShowChat->setEnabled(chat_events_ > 0);
    ui->actionShowComment->setEnabled(comment_events_ > 0);

    if (need_show_hide_) {
        for (int i = 0; i < ui->expertInfoTreeWidget->topLevelItemCount(); i++) {
            QTreeWidgetItem *group_ti = ui->expertInfoTreeWidget->topLevelItem(i);
            switch (group_ti->data(severity_col_, Qt::UserRole).value<int>()) {
            case PI_ERROR:
                group_ti->setHidden(! ui->actionShowError->isChecked());
                break;
            case PI_WARN:
                group_ti->setHidden(! ui->actionShowWarning->isChecked());
                break;
            case PI_NOTE:
                group_ti->setHidden(! ui->actionShowNote->isChecked());
                break;
            case PI_CHAT:
                group_ti->setHidden(! ui->actionShowChat->isChecked());
                break;
            case PI_COMMENT:
                group_ti->setHidden(! ui->actionShowComment->isChecked());
                break;
            default:
                break;
            }
        }
    }
    updateCounts();

    QString tooltip;
    QString hint;

    if (file_closed_) {
        tooltip = tr("Capture file closed.");
        hint = tr("Capture file closed.");
    } else if (display_filter_.isEmpty()) {
         tooltip = tr("No display filter");
         hint = tr("No display filter set.");
    } else {
        tooltip = tr("Limit information to \"%1\".").arg(display_filter_);
        hint = tr("Display filter: \"%1\"").arg(display_filter_);
    }

    ui->limitCheckBox->setToolTip(tooltip);
    hint.prepend("<small><i>");
    hint.append("</i></small>");
    ui->hintLabel->setText(hint);

}

void ExpertInfoDialog::actionShowToggled()
{
    need_show_hide_ = true;
    updateWidgets();
}

void ExpertInfoDialog::showProtoHierMenu(QPoint pos)
{
    bool enable = true;
    ExpertPacketTreeWidgetItem *packet_ti = dynamic_cast<ExpertPacketTreeWidgetItem *>(ui->expertInfoTreeWidget->currentItem());
    if (!packet_ti || packet_ti->hfId() < 0) {
        enable = false;
    }

    foreach (QMenu *submenu, ctx_menu_.findChildren<QMenu*>()) {
        submenu->setEnabled(enable && !file_closed_);
    }
    foreach (QAction *action, ctx_menu_.actions()) {
        FilterAction *fa = qobject_cast<FilterAction *>(action);
        bool action_enable = enable && !file_closed_;
        if (fa && (fa->action() == FilterAction::ActionWebLookup || fa->action() == FilterAction::ActionCopy)) {
            action_enable = enable;
        }
        action->setEnabled(action_enable);
    }

    ctx_menu_.popup(ui->expertInfoTreeWidget->viewport()->mapToGlobal(pos));
}

void ExpertInfoDialog::filterActionTriggered()
{
    ExpertPacketTreeWidgetItem *packet_ti = dynamic_cast<ExpertPacketTreeWidgetItem *>(ui->expertInfoTreeWidget->currentItem());
    FilterAction *fa = qobject_cast<FilterAction *>(QObject::sender());

    if (!fa || !packet_ti) {
        return;
    }

    int hf_index = packet_ti->hfId();
    if (hf_index > -1) {
        QString filter_string;
        if (fa->action() == FilterAction::ActionWebLookup) {
            filter_string = QString("%1 %2")
                    .arg(packet_ti->protocol())
                    .arg(packet_ti->summary());
        } else if (fa->action() == FilterAction::ActionCopy) {
            filter_string = QString("%1 %2: %3")
                    .arg(packet_ti->packetNum())
                    .arg(packet_ti->protocol())
                    .arg(packet_ti->summary());
        } else {
            filter_string = proto_registrar_get_abbrev(hf_index);
        }

        if (! filter_string.isEmpty()) {
            emit filterAction(filter_string, fa->action(), fa->actionType());
        }
    }
}

void ExpertInfoDialog::captureFileClosing()
{
    remove_tap_listener(this);
    WiresharkDialog::captureFileClosing();
}

void ExpertInfoDialog::on_expertInfoTreeWidget_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *)
{
    // Ignore top-level items.
    if (!current || !current->parent() || file_closed_) return;

    ExpertPacketTreeWidgetItem *packet_ti = dynamic_cast<ExpertPacketTreeWidgetItem *>(current);
    if (!packet_ti) return;

    emit goToPacket(packet_ti->packetNum(), packet_ti->hfId());
}

void ExpertInfoDialog::on_limitCheckBox_toggled(bool)
{
    retapPackets();
}

// Show child (packet list) items that match the contents of searchLineEdit.
void ExpertInfoDialog::on_searchLineEdit_textChanged(const QString &search_re)
{
    QTreeWidgetItemIterator it(ui->expertInfoTreeWidget, QTreeWidgetItemIterator::NoChildren);
    QRegExp regex(search_re, Qt::CaseInsensitive);
    while (*it) {
        bool hidden = true;
        if (search_re.isEmpty() || (*it)->text(packet_col_).contains(regex)) {
            hidden = false;
        }
        (*it)->setHidden(hidden);
        ++it;
    }
}

void ExpertInfoDialog::on_buttonBox_helpRequested()
{
    wsApp->helpTopicAction(HELP_EXPERT_INFO_DIALOG);
}

// Stat command + args

static void
expert_info_init(const char *, void*) {
    wsApp->emitStatCommandSignal("ExpertInfo", NULL, NULL);
}

static stat_tap_ui expert_info_stat_ui = {
    REGISTER_STAT_GROUP_GENERIC,
    NULL,
    "expert",
    expert_info_init,
    0,
    NULL
};

extern "C" {
void
register_tap_listener_qt_expert_info(void)
{
    register_stat_tap_ui(&expert_info_stat_ui, NULL);
}
}

/*
 * Editor modelines
 *
 * Local Variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
