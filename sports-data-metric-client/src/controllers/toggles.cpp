#include "toggles.h"

#include <QToolButton>
#include <QTabWidget>
#include <QList>
#include <QGroupBox>
#include <QObject>
#include <QWidget>
#include <QLayout>

/**
 * @brief Internal function toggles the visibility of a QGroupBox.
 *
 * Controls the visibility of the provided QGroupBox by setting its `visible` state.
 * When the `visible` parameter is true, the group box will be shown; if false, the
 * group box will be hidden.
 *
 * @param groupBox Pointer to the QGroupBox whose visibility is to be toggled.
 * @param visible Boolean indicating the desired visibility state.
 */
static void toggleGroupBoxVisibility(QGroupBox *groupBox, bool visible) {
    // Loop over all children of the group box
    for (QObject *child : groupBox->children()) {
        // Cast QObject to QWidget
        QWidget *widget = qobject_cast<QWidget *>(child);

        // Check if it's a valid QWidget (and not a layout)
        if (widget && child != groupBox->layout()) {
            widget->setVisible(visible);  // Set visibility to true or false
        }
    }
}

void setupGroupBoxToggles(QObject* context, const QList<GroupBoxToggle>& boxToggles) {
    for (const GroupBoxToggle& toggle : boxToggles) {
        QObject::connect(toggle.groupBox, &QGroupBox::toggled, context, [=](bool checked) {
            toggleGroupBoxVisibility(toggle.groupBox, checked);
        });
    }
}

void setupTabToggles(QTabWidget* tabWidget, const QList<TabToggle>& tabToggles) {
    for (const TabToggle& original : tabToggles) {
        TabToggle tabInfo = original;

        // Get initial tab index from the tabWidget
        if (tabInfo.index == -1) {
            tabInfo.index = tabWidget->indexOf(original.tabWidget);
        }
        tabInfo.visible = true;

        // Set button check state
        original.button->setCheckable(true);
        original.button->setChecked(true);

        QObject::connect(original.button, &QToolButton::clicked, tabWidget, [tabWidget, tabInfo]() mutable {
            if (tabInfo.visible) {
                // Remove the tab from the QTabWidget
                tabWidget->removeTab(tabWidget->indexOf(tabInfo.tabWidget));
                tabInfo.button->setChecked(false);
            } else {
                // Rebuild the icon with all states
                QIcon statefulIcon;
                statefulIcon.addPixmap(tabInfo.iconNormalOff.pixmap(24, 24), QIcon::Normal, QIcon::Off);
                statefulIcon.addPixmap(tabInfo.iconNormalOn.pixmap(24, 24), QIcon::Normal, QIcon::On);
                statefulIcon.addPixmap(tabInfo.iconDisabled.pixmap(24, 24), QIcon::Disabled);
                statefulIcon.addPixmap(tabInfo.iconActive.pixmap(24, 24), QIcon::Active);

                // Reinsert the tab with label and restored icon
                tabWidget->insertTab(tabInfo.index, tabInfo.tabWidget, statefulIcon, QString());
                tabWidget->setCurrentIndex(tabInfo.index);
                tabInfo.button->setChecked(true);
            }

            tabInfo.visible = !tabInfo.visible;

            // Hide or show tabWidget based on current tab count
            tabWidget->setVisible(tabWidget->count() > 0);
        });
    }
}
