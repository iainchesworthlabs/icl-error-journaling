// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// A stack of pages with the design's push and pop (Stage 4, package 4.4): the pages of Settings, from the list to an editor
// and back. It is a StackView of the Basic style, which supplies the stack, the item lifetimes and the hiding of the pages
// below the top one (so a screen reader reaches only the page in view), with every transition replaced:
//
//   push  the new page slides in from the right while the old one moves a third of the way left
//   pop   the reverse
//   replace  a fade
//   Reduce Motion: nothing slides; the pages fade in 100 ms
//
// The slides are animators, which run on the render thread and keep their rate while a commit holds the main thread, and
// take their times, curves and distances from Theme.motion. The stack draws nothing of its own: a page brings its
// background.
//
//     PageStack { anchors.fill: parent; initialItem: settingsList }
//     stack.push(editorComponent, { "title": qsTr("Locations") })
//     stack.pop()
StackView {
    id: root

    // What a screen reader says for the stack as a whole, which is a pane that holds the page in view.
    property string accessibleName: qsTr("Pages")

    // The numbers the transitions run on, from Theme.motion. With Reduce Motion nothing travels and the pages fade for
    // 100 ms; a test reads them here because the animators are inside the transitions.
    // How far the page that comes in, or goes out, slides across the stack.
    readonly property real slideDistance: Theme.motion.travel(width)
    // How far the page below moves while the top one slides over it.
    readonly property real parallaxDistance: Theme.motion.travel(width / 3)
    readonly property int transitionMs: Theme.motion.pageMs
    // The opacity a page comes in from and goes out to: unchanged when it slides, faded when it does not.
    readonly property real hiddenOpacity: Theme.reduceMotion ? 0 : 1

    clip: true
    Accessible.role: Accessible.Pane
    Accessible.name: root.accessibleName

    pushEnter: Transition {
        XAnimator {
            from: root.slideDistance
            to: 0
            duration: root.transitionMs
            easing.type: Theme.motion.easingSpring
        }
        OpacityAnimator {
            from: root.hiddenOpacity
            to: 1
            duration: root.transitionMs
        }
    }
    pushExit: Transition {
        XAnimator {
            from: 0
            to: -root.parallaxDistance
            duration: root.transitionMs
            easing.type: Theme.motion.easingSpring
        }
        OpacityAnimator {
            from: 1
            to: root.hiddenOpacity
            duration: root.transitionMs
        }
    }
    popEnter: Transition {
        XAnimator {
            from: -root.parallaxDistance
            to: 0
            duration: root.transitionMs
            easing.type: Theme.motion.easingSpring
        }
        OpacityAnimator {
            from: root.hiddenOpacity
            to: 1
            duration: root.transitionMs
        }
    }
    popExit: Transition {
        XAnimator {
            from: 0
            to: root.slideDistance
            duration: root.transitionMs
            easing.type: Theme.motion.easingSpring
        }
        OpacityAnimator {
            from: 1
            to: root.hiddenOpacity
            duration: root.transitionMs
        }
    }
    replaceEnter: Transition {
        OpacityAnimator {
            from: 0
            to: 1
            duration: root.transitionMs
        }
    }
    replaceExit: Transition {
        OpacityAnimator {
            from: 1
            to: 0
            duration: root.transitionMs
        }
    }
}
