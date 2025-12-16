Oleppy is OLEd Preservation in Python. It is a highly experimental project aimed at using the physics principles of OLED degradation in order to estimate OLED wear. 
The idea is this: OLED degradation is inversely proportional to cumulative hours used and brightness of an OLED. By making some assumptions on constants, we can then
estimate the damage to each OLED colour. This will give a vague sense of which media to play to reduce the damage, or if what you are watching is straining a particular
part of the scree.
Of course, at the end of the day, OLED technology has already improved massively, and it is likely that OLED burn-in is not going to be a major problem for you. Still
the wear maps look really cool, and who knows? It might actually show you something about your OLED screen habits.

Edit: Currently working on the python part. The current project is a triple buffered DXGI screen capture program that captures at variable frame rate. There are better screen capture software out there like ffmpeg which is free. However, I wanted to try building one, and learnt a lot in the process. Having said that, although it can be optimized much more, and there are probably 50+ optimization options that I am not aware of, the triple buffer already makes it fast enough to be usable.
