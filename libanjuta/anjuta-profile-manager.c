/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * anjuta-profile-manager.c
 * Copyright (C) Naba Kumar  <naba@gnome.org>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * SECTION:anjuta-profile-manager
 * @short_description: Managers a stack of plugins profiles
 * @see_also: #AnjutaPluginManager, #AnjutaProfile
 * @stability: Unstable
 * @include: libanjuta/anjuta-profile-manager.h
 * 
 * Anjuta uses up to three profiles. A system profile which contains mandatory
 * plugins which are never unloaded. A user profile is used when no project is
 * loaded and a project profile when one is loaded.
 * If a second project is loaded, it is loaded in another instance of Anjuta.
 * When a project is closed, Anjuta goes back to the user profile.
 *
 * The profile manager can be in a frozen state where you can push or 
 * pop a profile from the stack without triggering a change of the profile.
 */

#include <string.h>

#include <libanjuta/anjuta-debug.h>
#include <libanjuta/anjuta-marshal.h>
#include "anjuta-profile-manager.h"

enum
{
	PROP_0,
	PROP_PLUGIN_MANAGER,
	PROP_PROFILES
};

enum
{
	PROFILE_PUSHED,
	PROFILE_POPPED,
	LAST_SIGNAL
};

struct _AnjutaProfileManagerPriv
{
	AnjutaPluginManager *plugin_manager;
	GList *profiles;
	
	/* Pending queue. Profiles are queued until freeze count becomes 0 */
	GList *profiles_queue;
	
	/* Freeze count. Pending profiles are loaded when it reaches 0 */
	gint freeze_count;
};

static GObjectClass* parent_class = NULL;
static guint profile_manager_signals[LAST_SIGNAL] = { 0 };

static void
anjuta_profile_manager_init (AnjutaProfileManager *object)
{
	object->priv = g_new0 (AnjutaProfileManagerPriv, 1);
}

static void
anjuta_profile_manager_finalize (GObject *object)
{
	AnjutaProfileManagerPriv *priv = ANJUTA_PROFILE_MANAGER (object)->priv;

	if (priv->profiles)
		g_list_free_full (priv->profiles, g_object_unref);

	if (priv->profiles_queue)
		g_list_free_full (priv->profiles_queue, g_object_unref);


	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
anjuta_profile_manager_set_property (GObject *object, guint prop_id,
									 const GValue *value, GParamSpec *pspec)
{
	AnjutaProfileManagerPriv *priv;
	g_return_if_fail (ANJUTA_IS_PROFILE_MANAGER (object));
	priv = ANJUTA_PROFILE_MANAGER (object)->priv;
	
	switch (prop_id)
	{
	case PROP_PLUGIN_MANAGER:
		g_return_if_fail (ANJUTA_IS_PLUGIN_MANAGER (g_value_get_object (value)));
		priv->plugin_manager = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
anjuta_profile_manager_get_property (GObject *object, guint prop_id,
									 GValue *value, GParamSpec *pspec)
{
	AnjutaProfileManagerPriv *priv;
	g_return_if_fail (ANJUTA_IS_PROFILE_MANAGER (object));
	priv = ANJUTA_PROFILE_MANAGER (object)->priv;
	
	switch (prop_id)
	{
	case PROP_PLUGIN_MANAGER:
		g_value_set_object (value, priv->plugin_manager);
		break;
	case PROP_PROFILES:
		g_value_set_pointer (value, priv->profiles);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
anjuta_profile_manager_class_init (AnjutaProfileManagerClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

	object_class->finalize = anjuta_profile_manager_finalize;
	object_class->set_property = anjuta_profile_manager_set_property;
	object_class->get_property = anjuta_profile_manager_get_property;
	
	g_object_class_install_property (object_class,
	                                 PROP_PLUGIN_MANAGER,
	                                 g_param_spec_object ("plugin-manager",
	                                                      "Plugin Manager",
	                                                      "The plugin manager to use for profile plugins",
	                                                      ANJUTA_TYPE_PLUGIN_MANAGER,
	                                                      G_PARAM_READABLE |
														  G_PARAM_WRITABLE |
														  G_PARAM_CONSTRUCT));
	/**
	 * AnjutaProfileManager::profile-pushed:
	 * @profile_manager: a #AnjutaProfileManager object.
	 * @profile: the new #AnjutaProfile added.
	 * 
	 * Emitted when a profile is added in the stack. If the profile manager is
	 * not frozen, the current profile will be unloaded and the new one
	 * will be loaded.
	 */
	profile_manager_signals[PROFILE_PUSHED] =
		g_signal_new ("profile-pushed",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AnjutaProfileManagerClass,
									   profile_pushed),
		              NULL, NULL,
		              anjuta_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1,
		              ANJUTA_TYPE_PROFILE);
	
	/**
	 * AnjutaProfileManager::profile-popped:
	 * @profile_manager: a #AnjutaProfileManager object.
	 * @profile: the current removed #AnjutaProfile.
	 * 
	 * Emitted when a profile is removed from the stack. If the profile manager
	 * is not frozen, the current profile will be unloaded and the previous one
	 * will be loaded.
	 */
	profile_manager_signals[PROFILE_POPPED] =
		g_signal_new ("profile-popped",
		              G_OBJECT_CLASS_TYPE (klass),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (AnjutaProfileManagerClass,
									   profile_popped),
		              NULL, NULL,
					  anjuta_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1,
		              ANJUTA_TYPE_PROFILE);
	
}

GType
anjuta_profile_manager_get_type (void)
{
	static GType our_type = 0;

	if(our_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (AnjutaProfileManagerClass), /* class_size */
			(GBaseInitFunc) NULL, /* base_init */
			(GBaseFinalizeFunc) NULL, /* base_finalize */
			(GClassInitFunc) anjuta_profile_manager_class_init, /* class_init */
			(GClassFinalizeFunc) NULL, /* class_finalize */
			NULL /* class_data */,
			sizeof (AnjutaProfileManager), /* instance_size */
			0, /* n_preallocs */
			(GInstanceInitFunc) anjuta_profile_manager_init, /* instance_init */
			NULL /* value_table */
		};

		our_type = g_type_register_static (G_TYPE_OBJECT, "AnjutaProfileManager",
		                                   &our_info, 0);
	}

	return our_type;
}

/**
 * anjuta_profile_manager_new:
 * @plugin_manager: the #AnjutaPluginManager used by all profiles.
 * 
 * Create a new profile manager.
 *
 * Return value: the new #AnjutaProfileManager object.
 */
AnjutaProfileManager*
anjuta_profile_manager_new (AnjutaPluginManager *plugin_manager)
{
	GObject *obj;
	
	obj = g_object_new (ANJUTA_TYPE_PROFILE_MANAGER, "plugin-manager",
						plugin_manager, NULL);
	return ANJUTA_PROFILE_MANAGER (obj);
}

static gboolean
anjuta_profile_manager_load_profiles (AnjutaProfileManager *profile_manager, GError **error)
{
	AnjutaProfileManagerPriv *priv;
	gboolean loaded = FALSE;

	priv = profile_manager->priv;

	/* If there is no freeze load profile now */
	while ((priv->freeze_count <= 0) && (priv->profiles_queue != NULL))
	{
		AnjutaProfile *previous_profile = NULL;
		GList *node;

		/* We need to load each profile one by one because a "system" profile
		 * contains plugins which are never unloaded. */
		if (priv->profiles)
			previous_profile = priv->profiles->data;
		node = g_list_last (priv->profiles_queue);
		priv->profiles_queue = g_list_remove_link (priv->profiles_queue, node);
		priv->profiles = g_list_concat (node, priv->profiles);

		/* Load profile. Note that loading a profile can trigger the load of
		 * additional profile. Typically loading the default profile will
		 * trigger the load of the last project profile. */
		if (previous_profile != NULL) anjuta_profile_unload (previous_profile, NULL);
		loaded = anjuta_profile_load (ANJUTA_PROFILE (node->data), error);
	}

	return loaded;
}

static gboolean
anjuta_profile_manager_queue_profile (AnjutaProfileManager *profile_manager,
									  AnjutaProfile *profile,
									  GError **error)
{
	AnjutaProfileManagerPriv *priv;
	
	priv = profile_manager->priv;

	priv->profiles_queue = g_list_prepend (priv->profiles_queue, profile);

	/* If there is no freeze load profile now */
	return anjuta_profile_manager_load_profiles (profile_manager, error);
}

/**
 * anjuta_profile_manager_push:
 * @profile_manager: the #AnjutaProfileManager object.
 * @profile: the new #AnjutaProfile.
 * @error: error propagation and reporting.
 * 
 * Add a new profile at the top of the profile manager stack. If the profile
 * manager is not frozen, this new profile will be loaded immediatly and
 * become the current profile.
 *
 * Return value: %TRUE on success, %FALSE otherwise.
 */
gboolean
anjuta_profile_manager_push (AnjutaProfileManager *profile_manager,
							 AnjutaProfile *profile, GError **error)
{	
	g_return_val_if_fail (ANJUTA_IS_PROFILE_MANAGER (profile_manager), FALSE);

	/* Emit profile push signal */
	g_signal_emit_by_name (profile_manager, "profile-pushed",
						   profile);
	
	return anjuta_profile_manager_queue_profile (profile_manager, profile,
												 error);
}

/**
 * anjuta_profile_manager_pop:
 * @profile_manager: the #AnjutaProfileManager object.
 * @profile: the #AnjutaProfile to remove.
 * @error: error propagation and reporting.
 * 
 * Remove a profile from the profile manager stack. If the manager is not
 * frozen, only the current profile can be removed. It will be unloaded and
 * the previous profile will be loaded.
 * If the manager is frozen, the current profile or the last pushed profile
 * can be removed.
 *
 * Return value: %TRUE on success, %FALSE otherwise.
 */
gboolean
anjuta_profile_manager_pop (AnjutaProfileManager *profile_manager,
							AnjutaProfile *profile, GError **error)
{
	AnjutaProfileManagerPriv *priv;

	g_return_val_if_fail (ANJUTA_IS_PROFILE_MANAGER (profile_manager), FALSE);
	priv = profile_manager->priv;

	/* First check in the queue */
	if (priv->profiles_queue)
	{
		g_return_val_if_fail (priv->profiles_queue->data == profile, FALSE);
		priv->profiles_queue = g_list_remove (priv->profiles_queue, profile);
		
		g_signal_emit_by_name (profile_manager, "profile-popped",
							   profile);
		
		g_object_unref (profile);
		return TRUE;
	}
	
	/* Then check in the current stack */
	if (priv->profiles)
	{
		g_return_val_if_fail (priv->profiles->data == profile, FALSE);
		priv->profiles = g_list_remove (priv->profiles, profile);
		
		g_signal_emit_by_name (profile_manager, "profile-popped",
							   profile);
		
		/* Restore the next profile in the stack */
		anjuta_profile_unload (profile, NULL);
		g_object_unref (profile);
		if (priv->profiles)
		{
			return anjuta_profile_load (ANJUTA_PROFILE (priv->profiles->data), error);
		}
		return TRUE;
	}

	return FALSE;
}

/**
 * anjuta_profile_manager_freeze:
 * @profile_manager: the #AnjutaProfileManager object.
 * 
 * Freeze the plugin manager. In this state, plugins can be added and removed
 * from the stack without triggering any change in the current profile. It is
 * possible to freeze the manager several times but it will be back in its normal
 * state only after as much call of anjuta_profile_manager_thaw().
 */
void
anjuta_profile_manager_freeze (AnjutaProfileManager *profile_manager)
{
	AnjutaProfileManagerPriv *priv;
	g_return_if_fail (ANJUTA_IS_PROFILE_MANAGER (profile_manager));

	priv = profile_manager->priv;
	priv->freeze_count++;
}

/**
 * anjuta_profile_manager_thaw:
 * @profile_manager: the #AnjutaProfileManager object.
 * @error: error propagation and reporting.
 * 
 * Put back the plugin manager in its normal mode after calling
 * anjuta_profile_manager_freeze(). It will load a new profile if one has been
 * added while the manager was frozen.
 *
 * Return value: %TRUE on success, %FALSE otherwise.
 */
gboolean
anjuta_profile_manager_thaw (AnjutaProfileManager *profile_manager,
							GError **error)
{
	AnjutaProfileManagerPriv *priv;
	g_return_val_if_fail (ANJUTA_IS_PROFILE_MANAGER (profile_manager), FALSE);

	priv = profile_manager->priv;

	if (priv->freeze_count > 0)
		priv->freeze_count--;

	return anjuta_profile_manager_load_profiles (profile_manager, error);
}

/**
 * anjuta_profile_manager_get_current :
 * @profile_manager: A #AnjutaProfileManager object.
 * 
 * Return the current profile.
 *
 * Return value: (transfer none) (allow-none): a #AnjutaProfile object or %NULL
 * if the profile stack is empty.
 */
AnjutaProfile*
anjuta_profile_manager_get_current (AnjutaProfileManager *profile_manager)
{
	g_return_val_if_fail (ANJUTA_IS_PROFILE_MANAGER (profile_manager), NULL);

	if (profile_manager->priv->profiles_queue)
		return ANJUTA_PROFILE (profile_manager->priv->profiles_queue->data);
	else if (profile_manager->priv->profiles)
		return ANJUTA_PROFILE (profile_manager->priv->profiles->data);
	else
		return NULL;
}

/**
 * anjuta_profile_manager_close:
 * @profile_manager: A #AnjutaProfileManager object.
 *
 * Close the #AnjutaProfileManager causing "descoped" to be emitted and
 * all queued and previous profiles to be released. This function is to be used
 * when destroying an Anjuta instance.
 */
void
anjuta_profile_manager_close (AnjutaProfileManager *profile_manager)
{
	AnjutaProfileManagerPriv *priv;

	g_return_if_fail (ANJUTA_IS_PROFILE_MANAGER (profile_manager));

	priv = profile_manager->priv;

	if (priv->profiles)
	{
		AnjutaProfile *profile = ANJUTA_PROFILE (priv->profiles->data);

		/* Emit "descoped" so that other parts of anjuta can store
		 * information about the currently loaded profile. */
		anjuta_profile_unload (profile, NULL);

		g_list_free_full (priv->profiles, g_object_unref);
		priv->profiles = NULL;
	}

	if (priv->profiles_queue)
	{
		g_list_free_full (priv->profiles, g_object_unref);
		priv->profiles_queue = NULL;
	}
}
