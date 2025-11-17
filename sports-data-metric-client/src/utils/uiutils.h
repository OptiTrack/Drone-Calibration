#ifndef UIUTILS_H
#define UIUTILS_H

#include <QGroupBox>
#include <QVBoxLayout>

#include "toggles.h"

void addGroupBoxToUI(QWidget *parent, QGroupBox *groupBox);
void enableGroupBoxWidgets(QGroupBox *groupBox, bool enabled);

#endif // UIUTILS_H
