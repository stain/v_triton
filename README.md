# v_triton

This repository is a snapshot of [Netlab's Voyager Triton](http://trac.netlabs.org/v_triton) source code and is **no longer maintained**.

You may also need:

* https://github.com/stain/v_nom/
* https://github.com/stain/v_desktop/
* https://github.com/stain/v_triton/
* https://github.com/stain/v_doc/

## License/copyright

License and copyright was not explicit across the original source repository, but where in-line, is indicated as Dual-license CDDL 1.0/LGPL 2.1, e.g.:

```
/* ***** BEGIN LICENSE BLOCK *****
* Version: CDDL 1.0/LGPL 2.1
*
* The contents of this file are subject to the COMMON DEVELOPMENT AND
* DISTRIBUTION LICENSE (CDDL) Version 1.0 (the "License"); you may not use
* this file except in compliance with the License. You may obtain a copy of
* the License at http://www.sun.com/cddl/
*
* Software distributed under the License is distributed on an "AS IS" basis,
* WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
* for the specific language governing rights and limitations under the
* License.
*
* The Original Code is "NOM" Netlabs Object Model
*
* The Initial Developer of the Original Code is
* netlabs.org: Chris Wohlgemuth <cinc-ml@netlabs.org>.
* Portions created by the Initial Developer are Copyright (C) 2005-2007
* the Initial Developer. All Rights Reserved.
*
* Contributor(s):
*
* Alternatively, the contents of this file may be used under the terms of
* the GNU Lesser General Public License Version 2.1 (the "LGPL"), in which
* case the provisions of the LGPL are applicable instead of those above. If
* you wish to allow use of your version of this file only under the terms of
* the LGPL, and not to allow others to use your version of this file under
* the terms of the CDDL, indicate your decision by deleting the provisions
* above and replace them with the notice and other provisions required by the
* LGPL. If you do not delete the provisions above, a recipient may use your
* version of this file under the terms of any one of the CDDL or the LGPL.
*
* ***** END LICENSE BLOCK ***** */
```

Documentation adapted from <http://trac.netlabs.org/triton>:

# Welcome to Triton

***Triton*** is the name of the multimedia subsystem being developed for **Voyager**.
It is designed to be a modular and easily extendable, plugin-based system.

## About Triton

Triton itself is not a multimedia player. It is a well-defined subsystem, which contains
a core library to provide an easy-to-use API for the application developers to visualize and control
multimedia contents, and an easy-to-use API for the plugin developers to be able to extend the system
with support of new multimedia formats.

However, the Triton API was designed to be as simple and easy as possible, so that it can be
used anywhere. Writing a multimedia player based on it is very easy, and Triton itself even has some
sample applications for it.

## The Future

While Triton itself is only for playback of multimedia contents right now, it's planned that the system 
will be extended to be able to record them too, so that it will be possible to create such contents with 
Triton, or to transcode between different supported formats.

## Starting Points

 * A somewhat up to date [ToDo](http://trac.netlabs.org/v_triton/wiki/Triton_TODO) page for Triton


## Building it

See [this page](http://trac.netlabs.org/v_nom/wiki/BuildNom) for building it on OS/2.

See [this page](http://trac.netlabs.org/v_nom/wiki/BuildNomDarwin) for building it on Darwin (OS X).

See [this page](http://trac.netlabs.org/v_nom/wiki/BuildNomWindows) for building it on Windows.

See [this page](http://trac.netlabs.org/v_nom/wiki/BuildNomLinux) for building it on Linux.

