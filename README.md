## ARCHIVED

**You are probably looking for https://github.com/dolphin-emu/dolphin to contribute to this project**

The contents of this archive is a large number of discussions about pending merges. There is no time limit for this but I really want something like the command line `timedemo` commands I created. I like to track performance over the years with a standard demo loop that measures frame drawing time. Slow drawing is a notorious problem in emulators and a contentious issue over shortcuts. In Sweden we say a shortcut (genväg) becomes a longcut (senväg)

## Repo history

This project was hosted at https://code.google.com/p/dolphin-emu for many years. Fans of this lab mirrored it here in 2013. It was eventually moved from Google to https://github.com/dolphin-emu/dolphin in 2013 and this project was archived for old discussion's sake. For ten years we have wanted to transfer the ownership but this lab doesn't allow that without bothering the engineers or admins. The message is

> john-peterson already has a repository in the mirror/dolphin-emu network

And the decision was to keep it here and direct everyone that might land here to the right URL 

## Discussions archive

Some of the discussions contained here might still be relevant today:

- an homage to the legendary `timedemo` command in a ground breaking real-time renderer from 1996. The command would run recorded inputs without time synchronization. Essentially measuring how well your machine can render frames. The interested can read more under `/issues/26`

- changes related to live code editing (sometimes called "trainers" in the context of games)

- an herculean effort to reverse engineer the Wiimote especially for emulating the gyro. It is kept in one small patch that is still fun to read. And a much larger patch for the related debugging function `Spy()` that would spy on the hardware traffic. And finally a gigantic patch to actually use it with normal PC controls mouse and keyboard
