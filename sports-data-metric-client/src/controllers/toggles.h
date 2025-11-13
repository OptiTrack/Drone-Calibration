#ifndef TOGGLES_H
#define TOGGLES_H

#include <QToolButton>
#include <QTabWidget>
#include <QList>
#include <QGroupBox>

/**
 * @brief Represents a toggleable object for a single QGroupBox.
 *
 * The GroupBoxToggle structure holds a pointer to a toggleable QGroupBox.
 */
struct GroupBoxToggle {
    QGroupBox* groupBox;
};

/**
 * @brief Represents a toggleable object for a single tab in a QTabWidget.
 *
 * The TabToggle structure holds all necessary information for controlling the
 * visibility, icon state, and toggle behavior of a specific tab. This is typically
 * used in conjunction with a UI button to allow toggling individual tabs on or off.
 */
struct TabToggle {
    QToolButton* button;         // Button used to toggle the tab
    QWidget* tabWidget;          // Tab widget
    QIcon iconNormalOff;         // Icon for normal state (off)
    QIcon iconNormalOn;          // Icon for normal state (on)
    QIcon iconDisabled;          // Icon for disabled state
    QIcon iconActive;            // Icon for active state
    int index = -1;              // Original index in the QTabWidget
    bool visible = true;         // Visibility toggle state
};

/**
 * @brief Sets up toggle behavior for a list of GroupBoxToggle elements.
 *
 * Configures the toggle behavior for each GroupBoxToggle element in the provided
 * list and associates it within the given context.
 *
 * @param context Pointer to the QObject that provides the context.
 * @param boxToggles A list of GroupBoxToggle objects.
 */
void setupGroupBoxToggles(QObject* context, const QList<GroupBoxToggle>& boxToggles);

/**
 * @brief Sets up toggle behavior for the tabs in a QTabWidget.
 *
 * Configures tab toggling for a given QTabWidget based on a list of TabToggle objects.
 *
 * @param tabWidget Pointer to the QTabWidget whose tabs will be configured.
 * @param tabToggles A list of TabToggle objects describing how each tab should behave.
 *
 * @note The number of TabToggle entries should match the number of tabs in the tabWidget,
 * or the function may ignore extra entries or stop early depending on implementation.
 */
void setupTabToggles(QTabWidget* tabWidget, const QList<TabToggle>& tabToggles);

#endif // TOGGLES_H
