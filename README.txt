NIA: 204721 || 217612
Name: Berta Benet i Cugat || Carolina del Corral Farrarós
Mail: berta.benet01@estudiant.upf.edu || carolina.delcorral01@estudiant.upf.edu


/// USE ///

You can control the render and pipeline mode through the ImGUI or trough key commands

- Keys -

- z: Update Irradiance Cache

- ImGUI -

	Lights:
	- Visible: change visibility of light
	- Type: easily change type of light
	- Intensity: control how much power the light has (between 0 and 10)

		Directional:
		- Area Size: how much space the camera covers
		- Bias: to remove acne (default = 0.001)

		Point:
		- Max Distance: control how far the light has effect (between 10 and 5000)

		Spot:
		- Cone Angle: angle of light (between 0 and 90)
		- Exponent: reduce sharp borders of light cone (between 1 and 50)
		- Bias: to remove acne (default = 0.001)
		- Max Distance: control how far the light has effect (between 10 and 5000)

	Pipeline Mode:
	Change between FORWARD and DEFERRED

	Render Mode:
	Change between all the render modes available (FORWARD and DEFERRED ones)

	Blur SSAO+:
	Blur the SSAO+ or not

	HDR + Tonemapper:
	Enable or disable the Reinhard Tonemapper and the HDR (gamma space)

	Dithering:
	Enable or disable the dithering effect for transperencies

	Show probes:
	Render irradiance probes.

	Show reflection probe:
	Render reflection probe.

	Show Volumetric.

	Show DoF:
	Can change the plane in focus and the camera aperture with sliders.

	Show Glow:
	Can change the glow factor (how much glow you want) with slider.

	Show Chromatic Aberration:
	Can change the chromatic factor with slider.

	Show Lens Distortion:
	Can change the amount of distortion (negative or positive), anti-fisheye or fisheye.

- Shadowmaps -

We can see the shadowmap of each visible light (directional and spot) at the bottom of the window, they are shown in order of creation (as seen in the ImGUI). If one light is not visible anymore, its shadowmap will not appear on screen, as well as if one light changes type the corresponding shadowmap will appear/disappear.

Shadowmaps are only implemented in Multipass rendering, therefore they are only shown in that mode.


