# OLEPPY (OLEd Preservation in Python)
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
First paper - https://hal.science/hal-02465792/document - This one is often referenced in discussions of OLED burn in. It is a multi-factor study of OLED degradation across multiple factors such as driving current, temperature and time.
Second paper - https://www.mdpi.com/2076-3417/11/1/74?utm_source=chatgpt.com - for cross-referencing
If you do not like math, feel free to skip this section, because accurate prediction of OLED lifetime relies on equipment, not math. But we do not want to buy expansive equipment here, so let's make a vaguely correct estimate.
First,
```math
L \propto \left(\frac{RGB}{255}\right)^{2.2}
```
From GPT The sRGB color space specification (IEC 61966-2-1) defines a reference display gamma of approximately 2.2 (https://www.w3.org/Graphics/Color/srgb?utm_source=chatgpt.com)
Also GPT: For an OLED pixel:

Luminance ≈(Approx) proportional to current, not voltage

OLEDs have a non-linear I-V curve

Separately, it is usually agreed that the stretched exponential model works well for the early to mid life of an OLED. Typically, a consumer will stop using the OLED after L50, so this model is good enough. At any rate, wear rapidly accelerates after L50, and there isn't really a good model for the decay except for some suggestions of a separate exponential that is much steeper. At any rate, most agree that
```math
\frac{L(t)}{L_0}=\exp^{-(t/\tau)^{\beta}}
```
We propose using my limited knowledge from math modelling class to rewrite the stretched exponential as a damage accumulation model:
```math
\frac{L(t)}{L_0}=\exp^{-D(t)}, \quad \text{with} \quad D(t)={\left(\frac{t}{\tau}\right)}^{\beta}
```
This trick is common when trying to quantify an "effect" in this case magnitude of wear. What we want to define is the following:
```math
\Delta D_i={\left(\frac{\Delta t}{\tau\left(L_i\right)}\right)}^{\beta},\quad \text{where frame duration }=\Delta t\quad \text{and luminance at frame }i = L_i
```
So anyway using 5 frames for prototyping, the Total damage over 5 frames is:
```math
D_5 = \sum^{5}_{i=1}\Delta D_i
```
Now,
```math
\frac{L}{L_0} = \exp^{-D_5},
```
It has been tested sufficiently (I think?) that 
```math
\tau(L) \propto L^{-n}, \quad \text{with} \quad n=1.4-1.8
```
so
```math
\tau(L_i) = \tau_{Ref}{\left(\frac{L_i}{L_{Ref}}\right)}^{-n}
```
Substituting into damage increment:
```math
\Delta D_i={\left(\frac{\Delta t}{\tau_{Ref}}\right)}^{\beta}{\left(\frac{L_i}{L_{Ref}}\right)}^{n\beta}
```
Finally,
```math
\frac{L}{L_0} = \exp\left[-{\left(\frac{\Delta t}{\tau_{Ref}}\right)}^{\beta}\sum^5_{i=1}{\left(\frac{L_i}{L_{Ref}}\right)}^{n\beta}\right]
```
According to GPT at least, This is very close to what panel vendors internally use. We substitute (RGB/255)^2.2 for (I/I_0) because of the luminance current relation, then
```math
L(t)/L_0 = exp[-(t/T_Ref)(RGB/255)^{1.54}]
```
where T_ref=C_0/L^n_Ref and roughly T_ref=T_50, Also let's take T_50 = 20000 hrs
I am going to come back and tidy this up, but in the end without any means of getting real data this formula is going to end up wildly wrong. Should we get good data? No, because OLED manufacturers use secret techniques and proprietatry screen refresh and so on that makes the estimate far off anyway. The whole point is to detect very clear patterns in your viewing habits, if any, that will burn any OLED. So, let's stick with this.
Edit: On further inspection, the equation is really quite wrong. But anyway we have no real data to go on. So we search for obvious patterns.
# Results
We split this project into 3 parts. First, is a triple buffered DXGI screen capture program that captures at variable frame rate. There are better screen capture software out there like ffmpeg which is free. However, I wanted to try building one, and learnt a lot in the process. Having said that, although it can be optimized much more, and there are probably 50+ optimization options that I am not aware of, the triple buffer already makes it fast enough to be usable. You can compile this from source if you want.
Second is to release 1 minute screen capture binaries.
Third is to release a python script that shows, per frame, the color intensity for each color, which allows you to see which color may be overused. You can also extract the average effect for each color, and eventually estimate how long the OLED will last if you repeat your viewing habits. Of course, the result needs to be taken with a pinch of salt, because with so many OLED models and displays the equation used for estimation is going to be wrong for your particular display.
# Python script showcase
```Python
import cv2
import numpy as np

video_path = "/content/Wuthering Waves   2025-06-04 22-35-51.mp4"
cap = cv2.VideoCapture(video_path)

frame_num = 0

while cap.isOpened():
    ret, frame = cap.read()
    if not ret:
        break

    # OpenCV loads frames in BGR order, so convert to RGB
    frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

    # Split into separate R, G, B channels
    R = frame_rgb[:, :, 0].astype(np.float64)
    G = frame_rgb[:, :, 1].astype(np.float64)
    B = frame_rgb[:, :, 2].astype(np.float64)

    #print(f"Frame {frame_num}:")
    #print("R channel shape:", R.shape)
    #print("G channel shape:", G.shape)
    #print("B channel shape:", B.shape)

    # Example: access pixel (y=100, x=200)
    # print("R value:", R[100, 200])
    # print("G value:", G[100, 200])
    # print("B value:", B[100, 200])

    if frame_num == 0:
      R_sum = R
      G_sum = G
      B_sum = B
    else:
      R_sum += R
      G_sum += G
      B_sum += B

    # if frame_num < 10:
    #   print("R value:", R_sum[100, 200])
    #   print(f'frame_num: {frame_num}')

    frame_num += 1

cap.release()

R_sum = R_sum / frame_num
G_sum = G_sum / frame_num
B_sum = B_sum / frame_num

import matplotlib.pyplot as plt

plt.figure(figsize=(8,6))
plt.contourf(R_sum, cmap='inferno')  # filled contour
plt.title("Contour Plot of Red Channel")
plt.xlabel("X Pixels")
plt.ylabel("Y Pixels")
plt.colorbar(label="Intensity")
plt.show()
```
<img width="690" height="547" alt="snapshot A" src="https://github.com/user-attachments/assets/de4e50e5-9366-4700-a6bf-ed7d704ba8d5" />

```Python
import cv2
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from matplotlib.animation import FuncAnimation

# Load the video
video_path = "/content/Wuthering Waves   2025-06-04 22-35-51.mp4"
cap = cv2.VideoCapture(video_path)

# Read first frame to get dimensions
ret, frame = cap.read()
if not ret:
    raise ValueError("Could not read video.")

frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
height, width, _ = frame_rgb.shape

# Meshgrid for surface coordinates
step = 10  # downsample for speed
x = np.arange(0, width, step)
y = np.arange(0, height, step)
X, Y = np.meshgrid(x, y)

# Compute initial wear surface
R = frame_rgb[::step, ::step, 0].astype(np.float32)
wear = np.exp(-((R / 255.0) ** 1.54))

# Set up the figure
fig = plt.figure(figsize=(10, 7))
ax = fig.add_subplot(111, projection='3d')

surf = ax.plot_surface(X, Y, wear, cmap='plasma', edgecolor='none')

ax.set_zlim(0, 1)
ax.set_title("3D Wear Surface")
ax.set_xlabel("X Pixels")
ax.set_ylabel("Y Pixels")
ax.set_zlabel("Wear")

# Update function for each frame
def update(frame_idx):
    cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
    ret, frame = cap.read()
    if not ret:
        return surf

    frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

    R = frame_rgb[::step, ::step, 0].astype(np.float32)
    wear = np.exp(-((R / 255.0) ** 1.54))

    ax.clear()
    ax.plot_surface(X, Y, wear, cmap='viridis', edgecolor='none')
    ax.set_zlim(0, 1)
    ax.set_title(f"3D Wear Surface (Frame {frame_idx})")
    ax.set_xlabel("X Pixels")
    ax.set_ylabel("Y Pixels")
    ax.set_zlabel("Wear")

    return surf

# Create the animation
anim = FuncAnimation(
    fig,
    update,
    frames=range(0, int(cap.get(cv2.CAP_PROP_FRAME_COUNT)), 5),
    interval=100
)

anim.save("wear_surface_animation.mp4", fps=10, extra_args=['-vcodec', 'libx264'])

plt.show()
cap.release()
```
<img width="571" height="582" alt="Snapshot B" src="https://github.com/user-attachments/assets/20e7609a-cf9d-48c2-9e54-8a8648e4c00e" />

