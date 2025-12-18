#OLEPPY (OLEd Preservation in Python)
This is a highly experimental project aimed at using the physics principles of OLED degradation in order to estimate OLED wear. 
The idea is this: OLED degradation is inversely proportional to cumulative hours used and brightness of an OLED. By making some assumptions on constants, we can then
estimate the damage to each OLED colour. This will give a vague sense of which media to play to reduce the damage, or if what you are watching is straining a particular
part of the scree.
Of course, at the end of the day, OLED technology has already improved massively, see 
https://www.ossila.com/pages/oled-burn-in and 
https://www.reddit.com/r/Monitors/comments/1pc7f2e/deliberately_burning_in_my_qdoled_monitor_21/ 
and it is likely that OLED burn-in is not going to be a major problem for you. Still
the wear maps look really cool, and who knows? It might actually show you something about your OLED screen habits.

# Theoretical discussion
This project will be based off my reading of these 2 papers, as well as whatever knowledge summarization GPT cooks up. 
First paper - https://hal.science/hal-02465792/document - This one is often referenced in discussions of OLED burn in. It is a good multi-factor study of OLED degradation across multiple factors such as driving current, temperature and time.
Second paper - https://www.mdpi.com/2076-3417/11/1/74?utm_source=chatgpt.com - for cross-referencing
Luminance∝(RGB/255​)^2.2
From GPT The sRGB color space specification (IEC 61966-2-1) defines a reference display gamma of approximately 2.2 (https://www.w3.org/Graphics/Color/srgb?utm_source=chatgpt.com)
Also GPT: For an OLED pixel:

Luminance ≈(Approx) proportional to current, not voltage

OLEDs have a non-linear I-V curve

So we conclude
	​
C(J) = C_0(J/J_0)^(n-m)
where J_0 = reference current density, C_0 = fitted constant at J_0, n = luminance–lifetime exponent, m = current acceleration exponent
we try n = 1.5, m=2.3
So anyway roughly C ∝ (I/I_0)^0.8
Now,
L(t)/L_0 = exp[-(t/T_50)^B],
and apparently we use B=1.5
so
L(t)/L_0 = exp[-(t/T_Ref)(I/I_0)^-0.8(RGB/255)^3.3]
According to GPT at least, This is very close to what panel vendors internally use. We substitute (RGB/255)^2.2 for (I/I_0) because of the luminance current relation, then
L(t)/L_0 = exp[-(t/T_Ref)(RGB/255)^1.54], where T_ref=C_0/L^n_Ref and roughly T_ref=T_50, Also let's take T_50 = 20000 hrs
I am going to come back and tidy this up, but in the end without any means of getting real data this formula is going to end up wildly wrong. Should we get good data? No, because OLED manufacturers use secret techniques and proprietatry screen refresh and so on that makes the estimate far off anyway. The whole point is to detect very clear patterns in your viewing habits, if any, that will burn any OLED. So, let's stick with this.

Edit: Currently working on the python part. The current project is a triple buffered DXGI screen capture program that captures at variable frame rate. There are better screen capture software out there like ffmpeg which is free. However, I wanted to try building one, and learnt a lot in the process. Having said that, although it can be optimized much more, and there are probably 50+ optimization options that I am not aware of, the triple buffer already makes it fast enough to be usable.
