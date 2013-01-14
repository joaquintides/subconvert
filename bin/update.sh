#!/bin/bash -x

set -o errexit

WORKSPACE="$(pwd)"
RAMDISK="/tmp/jenkins-subconvert"  # /tmp is expected to be a ramdisk
SUBCONVERT="$WORKSPACE/../subconvert"
SCRIPTS="$SUBCONVERT/subconvert/bin"
DOC="$SUBCONVERT/subconvert/doc"

#(cd "$WORKSPACE/mirrors/modularized"
#    && git status --porcelain | awk '{print $2}' | xargs rm -fr)

#(cd boost-git; git reset --hard HEAD; git pull; \
# git submodule foreach "git reset --hard HEAD"; \
# git submodule update --init)
# (cd boost-private; git pull)
# (cd boost-svn; git pull)
# (cd Boost.Defrag; git pull)
# (cd installer; git pull)
# (cd ryppl; git pull)
# (cd boost-modularize; git pull)

# perl -i -pe "s%url =.*%url = file://$PWD/boost.svnrepo%;" boost-clone/.git/config
# (cd boost-clone; git svn fetch; git reset --hard trunk)

mkdir -p "$RAMDISK"
chmod 700 "$RAMDISK"
if [[ ! -d "$RAMDISK/cpp" ]]; then
    mkdir "$RAMDISK/cpp"
    cd "$RAMDISK/cpp"
    git init
fi
cd "$RAMDISK/cpp"

export LD_LIBRARY_PATH="$SUBCONVERT/prefix/lib"

"$SUBCONVERT/prefix/bin/subconvert"                  \
       -A "$DOC/authors.txt"                         \
       -B "$DOC/branches.txt"                        \
       -M "$DOC/modules.txt"                         \
       convert /home/svnsync/dump/boost.svndump

git symbolic-ref HEAD refs/heads/trunk
git prune
sleep 5

git remote add origin git@github.com:ryppl/boost-history.git
git push -f --all origin
git push -f --mirror origin
git push -f --tags origin

sleep 5

# rsync to persistent storage.  Give ETA updates every 2s
rsync -aix --delete .git/ $WORKSPACE/boost-history.git/ | pv -le -s "$(du -d 0 boost-zero/ | cut -f 1)" > /dev/null

cd $WORKSPACE
fi
