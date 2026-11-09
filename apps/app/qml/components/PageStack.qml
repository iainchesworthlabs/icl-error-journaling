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
//   replace, and every transition under Reduce Motion: a fade of 100 ms, and nothing slides
//
// Each transition holds one kind of animation and never both. An XAnimator and an OpacityAnimator in one Transition leave
// the pages at opacity 0 after a push, even when the opacity animator goes from 1 to 1, so a slide has no opacity
// animation and a fade has no XAnimator. The slides are animators, which run on the render thread and keep their rate while
// a commit holds the main thread; they take their times, curves and distances from Theme.motion. The stack draws nothing
// of its own: a page brings its background.
//
//     PageStack { anchors.fill: parent; initialItem: settingsList }
//     stack.push(editorComponent, { "title": qsTr("Locations") })
//     stack.pop()
StackView {
    id: root

    // What a screen reader says for the stack as a whole, which is a pane that holds the page in view.
    property string accessibleName: qsTr("Pages")

    // The numbers the transitions run on, from Theme.motion; a test reads them here because the animators are inside
    // the transitions.
    // How far the page that comes in, or goes out, slides across the stack.
    readonly property real slideDistance: width
    // How far the page below moves while the top one slides over it.
    readonly property real parallaxDistance: width / 3
    readonly property int transitionMs: Theme.motion.pageMs
    // Whether the pages fade instead of sliding (Reduce Motion).
    readonly property bool fading: Theme.reduceMotion

    // The transitions the stack uses, by kind.
    readonly property Transition slideIn: Transition {
        XAnimator {
            from: root.slideDistance
            to: 0
            duration: root.transitionMs
            easing.type: Theme.motion.easingSpring
        }
    }
    readonly property Transition slideOut: Transition {
        XAnimator {
            from: 0
            to: root.slideDistance
            duration: root.transitionMs
            easing.type: Theme.motion.easingSpring
        }
    }
    readonly property Transition slideUnder: Transition {
        XAnimator {
            from: 0
            to: -root.parallaxDistance
            duration: root.transitionMs
            easing.type: Theme.motion.easingSpring
        }
    }
    readonly property Transition slideBack: Transition {
        XAnimator {
            from: -root.parallaxDistance
            to: 0
            duration: root.transitionMs
            easing.type: Theme.motion.easingSpring
        }
    }
    readonly property Transition fadeIn: Transition {
        OpacityAnimator {
            from: 0
            to: 1
            duration: root.transitionMs
        }
    }
    readonly property Transition fadeOut: Transition {
        OpacityAnimator {
            from: 1
            to: 0
            duration: root.transitionMs
        }
    }

    clip: true
    Accessible.role: Accessible.Pane
    Accessible.name: root.accessibleName

    pushEnter: root.fading ? root.fadeIn : root.slideIn
    pushExit: root.fading ? root.fadeOut : root.slideUnder
    popEnter: root.fading ? root.fadeIn : root.slideBack
    popExit: root.fading ? root.fadeOut : root.slideOut
    replaceEnter: root.fadeIn
    replaceExit: root.fadeOut
}
