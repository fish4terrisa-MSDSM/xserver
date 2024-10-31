CONTRIBUTING TO XSERVER
======================

Preparation
-----------

* become confident with git and gitlab (if you aren't already)
* join the xorg-devel mailing list: https://lists.freedesktop.org/mailman/listinfo/xorg
* create an gitlab account on https://gitlab.freedesktop.org
* request fork permissions: https://gitlab.freedesktop.org/freedesktop/freedesktop/-/wikis/home#how-can-i-contribute-to-an-existing-project-or-create-a-new-one
* fork xserver repo: https://gitlab.freedesktop.org/xorg/xserver

Patch/commit quality
--------------------

* each commit should solve one particular problem and be self-consistent, ie. complete in itself and not break anything else
* preparational steps should be done in separate commits
* describe what the change does and why it's necessary within the commit message
* commit title should contain prefix telling which parts are touched (eg. "modesetting: ", "xwayland: ", "Xext: ", ...)
* if a commit fixes an issue (ticket), add an "Closes: ..." header (with link to issue) to the commit message
* if a commit fixes an regression by another commit, add an "Fixes: " header (with broken commit's id) to commit message
* commits to be merged need to by signed-off by the author
* example: https://gitlab.freedesktop.org/xorg/xserver/-/commit/3ddb81b

Submission
----------

* separate issues should be submitted separately (as separate branches)
* file a pull request in gitlab
* if it should be backported to other branches (eg. 21.x releases), mention this is the PR description
* use the "draft" flag if the submission is just for review/discussion instead of merging to mainline
* make sure each individual commits builds all tests are green (important for bisect)
* be patient !

Further reading
---------------

* https://www.x.org/wiki/Development/Documentation/SubmittingPatches/
* https://gitlab.freedesktop.org/freedesktop/freedesktop/-/wikis/home#how-can-i-contribute-to-an-existing-project-or-create-a-new-one
