/**
 * Copyright 2013 Dolphin Emulator Project
 * Licensed under GPLv2
 * Refer to the license.txt file included.
 */

package org.dolphinemu.dolphinemu.settings;

import org.dolphinemu.dolphinemu.NativeLibrary;

import android.content.Context;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;

/**
 * A class that retrieves all of the set user preferences in Android, in a safe way.
 * <p>
 * If any preferences are added to this emulator, an accessor for that preference
 * should be added here. This way lengthy calls to getters from SharedPreferences
 * aren't made necessary.
 */
public final class UserPreferences
{
	/**
	 * Loads the settings stored in the Dolphin ini config files to the shared preferences of this front-end.
	 * 
	 * @param ctx The context used to retrieve the SharedPreferences instance.
	 */
	public static void LoadIniToPrefs(Context ctx)
	{
		SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(ctx);

		// Get an editor.
		SharedPreferences.Editor editor = prefs.edit();

		// Add the settings.
		editor.putString("cpuCorePref",   getConfig("Core", "CPUCore", "3"));
		editor.putBoolean("dualCorePref", getConfig("Core", "CPUThread", "False").equals("True"));
		editor.putBoolean("fastmemPref", getConfig("Core", "Fastmem", "False").equals("True"));

		editor.putString("gpuPref",               getConfig("Core", "GFXBackend", "Software Renderer"));
		editor.putBoolean("showFPS",              getConfig("Video_Settings", "ShowFPS", "False").equals("True"));
		editor.putBoolean("drawOnscreenControls", getConfig("Android", "ScreenControls", "True").equals("True"));

		editor.putString("internalResolution",     getConfig("Video_Settings", "EFBScale", "2") );
		editor.putString("FSAA",                   getConfig("Video_Settings", "MSAA", "0"));
		editor.putString("anisotropicFiltering",   getConfig("Video_Enhancements", "MaxAnisotropy", "0"));
		editor.putBoolean("scaledEFBCopy",         getConfig("Video_Hacks", "EFBScaleCopy", "True").equals("True"));
		editor.putBoolean("perPixelLighting",      getConfig("Video_Settings", "EnablePixelLighting", "False").equals("True"));
		editor.putBoolean("forceTextureFiltering", getConfig("Video_Enhancements", "ForceFiltering", "False").equals("True"));
		editor.putBoolean("disableFog",            getConfig("Video_Settings", "DisableFog", "False").equals("True"));
		editor.putBoolean("skipEFBAccess",         getConfig("Video_Hacks", "EFBAccessEnable", "False").equals("True"));
		editor.putBoolean("ignoreFormatChanges",   getConfig("Video_Hacks", "EFBEmulateFormatChanges", "False").equals("False"));

		String efbCopyOn     = getConfig("Video_Hacks", "EFBCopyEnable", "False");
		String efbToTexture  = getConfig("Video_Hacks", "EFBToTextureEnable", "False");
		String efbCopyCache  = getConfig("Video_Hacks", "EFBCopyCacheEnable", "False");

		if (efbCopyOn.equals("False"))
		{
			editor.putString("efbCopyMethod", "Off");
		}
		else if (efbCopyOn.equals("True") && efbToTexture.equals("True"))
		{
			editor.putString("efbCopyMethod", "Texture");
		}
		else if(efbCopyOn.equals("True") && efbToTexture.equals("False") && efbCopyCache.equals("False"))
		{
			editor.putString("efbCopyMethod", "RAM (uncached)");
		}
		else if(efbCopyOn.equals("True") && efbToTexture.equals("False") && efbCopyCache.equals("True"))
		{
			editor.putString("efbCopyMethod", "RAM (cached)");
		}

		editor.putString("textureCacheAccuracy", getConfig("Video_Settings", "SafeTextureCacheColorSamples", "128"));

		String usingXFB = getConfig("Video_Settings", "UseXFB", "False");
		String usingRealXFB = getConfig("Video_Settings", "UseRealXFB", "False");

		if (usingXFB.equals("False"))
		{
			editor.putString("externalFrameBuffer", "Disabled");
		}
		else if (usingXFB.equals("True") && usingRealXFB.equals("False"))
		{
			editor.putString("externalFrameBuffer", "Virtual");
		}
		else if (usingXFB.equals("True") && usingRealXFB.equals("True"))
		{
			editor.putString("externalFrameBuffer", "Real");
		}

		editor.putBoolean("cacheDisplayLists",       getConfig("Video_Hacks", "DlistCachingEnable", "False").equals("True"));
		editor.putBoolean("disableDestinationAlpha", getConfig("Video_Settings", "DstAlphaPass", "False").equals("True"));
		editor.putBoolean("fastDepthCalculation",    getConfig("Video_Settings", "FastDepthCalc", "True").equals("True"));

		// Apply the changes.
		editor.commit();
	}

	// Small utility method that shortens calls to NativeLibrary.GetConfig.
	private static String getConfig(String section, String key, String defaultValue)
	{
		return NativeLibrary.GetConfig(section, key, defaultValue);
	}

	/** 
	 * Writes the preferences set in the front-end to the Dolphin ini files.
	 * 
	 * @param ctx The context used to retrieve the user settings.
	 * */
	public static void SavePrefsToIni(Context ctx)
	{
		SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(ctx);

		// Whether or not the user is using dual core.
		boolean isUsingDualCore = prefs.getBoolean("dualCorePref", true);

		// Current CPU core being used. Falls back to interpreter upon error.
		String currentEmuCore   = prefs.getString("cpuCorePref", "0");

		// Fastmem JIT core usage
		boolean isUsingFastmem = prefs.getBoolean("fastmemPref", false);

		// Current video backend being used. Falls back to software rendering upon error.
		String currentVideoBackend = prefs.getString("gpuPref", "Software Rendering");

		// Whether or not FPS will be displayed on-screen.
		boolean showingFPS = prefs.getBoolean("showFPS", false);

		// Whether or not to draw on-screen controls.
		boolean drawingOnscreenControls = prefs.getBoolean("drawOnscreenControls", true);

		// Whether or not to ignore all EFB access requests from the CPU.
		boolean skipEFBAccess = prefs.getBoolean("skipEFBAccess", false);

		// Whether or not to ignore changes to the EFB format.
		boolean ignoreFormatChanges = prefs.getBoolean("ignoreFormatChanges", false);

		// EFB copy method to use.
		String efbCopyMethod = prefs.getString("efbCopyMethod", "Off");

		// Texture cache accuracy. Falls back to "Fast" up error.
		String textureCacheAccuracy = prefs.getString("textureCacheAccuracy", "128");

		// External frame buffer emulation. Falls back to disabled upon error.
		String externalFrameBuffer = prefs.getString("externalFrameBuffer", "Disabled");

		// Whether or not display list caching is enabled.
		boolean dlistCachingEnabled = prefs.getBoolean("cacheDisplayLists", false);

		// Whether or not to disable destination alpha.
		boolean disableDstAlphaPass = prefs.getBoolean("disableDestinationAlpha", false);

		// Whether or not to use fast depth calculation.
		boolean useFastDepthCalc = prefs.getBoolean("fastDepthCalculation", true);

		// Internal resolution. Falls back to 1x Native upon error.
		String internalResolution = prefs.getString("internalResolution", "2");

		// FSAA Level. Falls back to 1x upon error.
		String FSAALevel = prefs.getString("FSAA", "0");

		// Anisotropic Filtering Level. Falls back to 1x upon error.
		String anisotropicFiltLevel = prefs.getString("anisotropicFiltering", "0");

		// Whether or not Scaled EFB copies are used.
		boolean usingScaledEFBCopy = prefs.getBoolean("scaledEFBCopy", true);

		// Whether or not per-pixel lighting is used.
		boolean usingPerPixelLighting = prefs.getBoolean("perPixelLighting", false);

		// Whether or not texture filtering is being forced.
		boolean isForcingTextureFiltering = prefs.getBoolean("forceTextureFiltering", false);

		// Whether or not fog is disabled.
		boolean fogIsDisabled = prefs.getBoolean("disableFog", false);


		// CPU related Settings
		NativeLibrary.SetConfig("Core", "CPUCore", currentEmuCore);
		NativeLibrary.SetConfig("Core", "CPUThread", isUsingDualCore ? "True" : "False");
		NativeLibrary.SetConfig("Core", "Fastmem", isUsingFastmem ? "True" : "False");

		// General Video Settings
		NativeLibrary.SetConfig("Core", "GFXBackend", currentVideoBackend);
		NativeLibrary.SetConfig("Video_Settings", "ShowFPS", showingFPS ? "True" : "False");
		NativeLibrary.SetConfig("Android", "ScreenControls", drawingOnscreenControls ? "True" : "False");

		// Video Hack Settings
		NativeLibrary.SetConfig("Video_Hacks", "EFBAccessEnable", skipEFBAccess ? "False" : "True");
		NativeLibrary.SetConfig("Video_Hacks", "EFBEmulateFormatChanges", ignoreFormatChanges ? "True" : "False");

		// Set EFB Copy Method 
		if (efbCopyMethod.equals("Off"))
		{
			NativeLibrary.SetConfig("Video_Hacks", "EFBCopyEnable", "False");
		}
		else if (efbCopyMethod.equals("Texture"))
		{
			NativeLibrary.SetConfig("Video_Hacks", "EFBCopyEnable", "True");
			NativeLibrary.SetConfig("Video_Hacks", "EFBToTextureEnable", "True");
		}
		else if (efbCopyMethod.equals("RAM (uncached)"))
		{
			NativeLibrary.SetConfig("Video_Hacks", "EFBCopyEnable", "True");
			NativeLibrary.SetConfig("Video_Hacks", "EFBToTextureEnable", "False");
			NativeLibrary.SetConfig("Video_Hacks", "EFBCopyCacheEnable", "False");
		}
		else if (efbCopyMethod.equals("RAM (cached)"))
		{
			NativeLibrary.SetConfig("Video_Hacks", "EFBCopyEnable", "True");
			NativeLibrary.SetConfig("Video_Hacks", "EFBToTextureEnable", "False");
			NativeLibrary.SetConfig("Video_Hacks", "EFBCopyCacheEnable", "True");
		}

		// Set texture cache accuracy
		NativeLibrary.SetConfig("Video_Settings", "SafeTextureCacheColorSamples", textureCacheAccuracy);

		// Set external frame buffer.
		if (externalFrameBuffer.equals("Disabled"))
		{
			NativeLibrary.SetConfig("Settings", "UseXFB", "False");
		}
		else if (externalFrameBuffer.equals("Virtual"))
		{
			NativeLibrary.SetConfig("Video_Settings", "UseXFB", "True");
			NativeLibrary.SetConfig("Video_Settings", "UseRealXFB", "False");
		}
		else if (externalFrameBuffer.equals("Real"))
		{
			NativeLibrary.SetConfig("Video_Settings", "UseXFB", "True");
			NativeLibrary.SetConfig("Video_Settings", "UseRealXFB", "True");
		}

		NativeLibrary.SetConfig("Video_Hacks", "DlistCachingEnable", dlistCachingEnabled ? "True" : "False");
		NativeLibrary.SetConfig("Video_Settings", "DstAlphaPass", disableDstAlphaPass ? "True" : "False");
		NativeLibrary.SetConfig("Video_Settings", "FastDepthCalc", useFastDepthCalc ? "True" : "False");

		//-- Enhancement Settings --//
		NativeLibrary.SetConfig("Video_Settings", "EFBScale", internalResolution);
		NativeLibrary.SetConfig("Video_Settings", "MSAA", FSAALevel);
		NativeLibrary.SetConfig("Video_Enhancements", "MaxAnisotropy", anisotropicFiltLevel);
		NativeLibrary.SetConfig("Video_Hacks", "EFBScaledCopy", usingScaledEFBCopy ? "True" : "False");
		NativeLibrary.SetConfig("Video_Settings", "EnablePixelLighting", usingPerPixelLighting ? "True" : "False");
		NativeLibrary.SetConfig("Video_Enhancements", "ForceFiltering", isForcingTextureFiltering ? "True" : "False");
		NativeLibrary.SetConfig("Video_Settings", "DisableFog", fogIsDisabled ? "True" : "False");
	}
}
