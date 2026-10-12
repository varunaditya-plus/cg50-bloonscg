<div align="center">

# Bloons TD (for the fx-CG50)

Bloons CG ports the first level from Bloons TD6 (the most iconic tower defense game) to the Casio fx-CG50 calculator. This project is not finished is missing some towers/upgrade paths, but works pretty well for the most part.

<img width="396" height="224" alt="An example game" src="https://github.com/user-attachments/assets/bdd472ef-5183-492e-9677-d2d85f2c965b" />
<img width="396" height="224" alt="Upgrades menu" src="https://github.com/user-attachments/assets/2ab9d3fb-9428-4832-88d2-710c61982ca8" />


</div>

<h2>↓ Installation</h2>

1. Using the Mini-USB to USB cable that came with your calculator, connect the calculator to your laptop
2. Click F1 when the connection menu shows up on your calculator
3. A USB drive should show up on your laptop. Drag and drop the .g3a file into the drive's root directory.
4. Eject the drive, and check your calculator. You should now have Bloons CG!

<h2>↓ Building</h2>

### Requirements
- [Gint](https://git.planet-casio.com/Lephenixnoir/gint) and [fxSDK](https://git.planet-casio.com/Lephenixnoir/fxsdk)

### Commands
```bash
$ git clone https://github.com/varunaditya-plus/cg50-blockblast.git
$ fxsdk build-cg
```

<h2>✰ About</h2>
I got really into Bloons TD 6 recently, and after hearing that AI had gotten really good at decompiling/working with game code, I wanted to see if it could help me port the first level from BTD6 to my calculator. I used it to review a decompiled version of BTD6 and find everything needed to make the game. I then made a few UI designs in Figma before writing the add-in in C. FxSDK isn't a complex SDK, so writing most of this add-in was pretty straightforward.<br/><br/>The actual game code was much more complex, so I let Codex sift through it and write it, and I just added a few minor tweaks afterward to make the functionality nicer. A few features from the original game aren't in this variation because it would be too computationally complex (and probably crash your calculator), but I might try adding them soon.  

<br /><br />
∗ Made by Varun<br >
Copyright © 2026 ⁠[thevarunaditya](https://github.com/thevarunaditya)
