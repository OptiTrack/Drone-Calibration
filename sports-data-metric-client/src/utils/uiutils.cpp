#include "uiutils.h"

void addGroupBoxToUI(QWidget *parent, QGroupBox *groupBox)
{

    // Group Box Toggle List
    QList<GroupBoxToggle> groupBoxToggles = {{ groupBox }};

    setupGroupBoxToggles(parent, groupBoxToggles);

    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(parent->layout());
    if (layout) {
        int spacerIndex = -1;
        for (int i = 0; i < layout->count(); ++i) {
            QLayoutItem *item = layout->itemAt(i);
            if (item->spacerItem()) {
                spacerIndex = i;
                break;
            }
        }

        if (spacerIndex != -1) {
            layout->insertWidget(spacerIndex, groupBox);
        } else {
            layout->addWidget(groupBox);
        }
    }
}

void enableGroupBoxWidgets(QGroupBox *groupBox, bool enabled) {
    const auto &children = groupBox->findChildren<QWidget*>();
    for (QWidget *childWidget : children) {
        childWidget->setEnabled(enabled);
    }
}
