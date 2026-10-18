-- Extended requests 121-130 and their exact CacheD model owners. Current
-- Cached.exe and the paired current L2Server.exe fix every packet, callable
-- and result-column ABI used below. High Five, Ertheia, modified-287 and
-- Classic 3.0 SQL donors agree on the UI, in-zone, pledge and item effects.
-- No supplied SQL archive contains lin_UpdateTutorial. That one body is
-- reconstructed from CUser::SaveTutorial's exact binary/char-id
-- bindings and the current user_data.tutorial_flag load/storage owner.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_updatetutorial"(
    p_tutorial_flag bytea,
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET "tutorial_flag" = p_tutorial_flag
    WHERE "char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadinzonerestriction"(
    p_char_id integer
) RETURNS TABLE(
    inzone_type_id integer,
    group_restriction integer,
    last_use integer,
    entrance_count integer,
    max_entrance_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT restriction."inzone_type_id",
           restriction."group_restriction",
           restriction."last_use",
           restriction."entrance_count",
           restriction."max_entrance_count"
    FROM "world"."user_inzone" AS restriction
    WHERE restriction."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_markinzonerestriction"(
    p_char_id integer,
    p_inzone_type_id integer,
    p_group_restriction integer,
    p_last_use integer,
    p_entrance_count integer,
    p_max_entrance_count integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_inzone" AS restriction
    SET "last_use" = p_last_use,
        "entrance_count" = p_entrance_count,
        "max_entrance_count" = p_max_entrance_count
    WHERE restriction."char_id" = p_char_id
      AND restriction."inzone_type_id" = p_inzone_type_id
      AND restriction."group_restriction" = p_group_restriction;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_inzone" (
            "char_id",
            "inzone_type_id",
            "group_restriction",
            "last_use",
            "entrance_count",
            "max_entrance_count"
        ) VALUES (
            p_char_id,
            p_inzone_type_id,
            p_group_restriction,
            p_last_use,
            p_entrance_count,
            p_max_entrance_count
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveinzoneresricthistory"(
    p_char_id integer,
    p_inzone_type_id integer,
    p_use_time integer,
    p_inzone_uid integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_inzone_history" AS history
    SET "use_time" = p_use_time
    WHERE history."char_id" = p_char_id
      AND history."inzone_uid" = p_inzone_uid;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_inzone_history" (
            "char_id",
            "inzone_uid",
            "inzone_type_id",
            "use_time"
        ) VALUES (
            p_char_id,
            p_inzone_uid,
            p_inzone_type_id,
            p_use_time
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetinzonerestriction"(
    p_char_id integer,
    p_inzone_id integer DEFAULT -1
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_inzone_id = -1 THEN
        DELETE FROM "world"."user_inzone"
        WHERE "char_id" = p_char_id;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_inzone_history" AS history
        JOIN "world"."user_data" AS character
          ON history."inzone_uid" = character."associated_inzone_uid"
        WHERE history."inzone_type_id" = p_inzone_id
          AND history."char_id" = p_char_id
    ) THEN
        UPDATE "world"."user_data"
        SET "associated_inzone_uid" = 0,
            "associated_inzone" = 0
        WHERE "char_id" = p_char_id;
    END IF;

    DELETE FROM "world"."user_inzone"
    WHERE "char_id" = p_char_id
      AND "inzone_type_id" = p_inzone_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getui"(
    p_char_id integer
) RETURNS TABLE(
    ui_setting_size integer,
    ui_setting bytea
)
LANGUAGE sql
STABLE
AS $function$
    SELECT setting."ui_setting_size"::integer,
           setting."ui_setting"
    FROM "world"."user_ui" AS setting
    WHERE setting."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveui"(
    p_char_id integer,
    p_ui_setting_size integer,
    p_ui_setting bytea
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_ui" AS setting
    SET "ui_setting_size" = p_ui_setting_size::smallint,
        "ui_setting" = p_ui_setting
    WHERE setting."char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_ui" (
            "char_id",
            "ui_setting_size",
            "ui_setting"
        ) VALUES (
            p_char_id,
            p_ui_setting_size::smallint,
            p_ui_setting
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateitemslotindex"(
    p_item_id integer,
    p_slot_index integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_item"
    SET "inventory_slot_index" = p_slot_index
    WHERE "item_id" = p_item_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setpledgecastlesiegedefencecount"(
    p_pledge_id integer,
    p_siege_count integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge"
    SET "castle_siege_defence_count" = p_siege_count
    WHERE "pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setassociatedinzone"(
    p_char_id integer,
    p_inzone_id integer,
    p_inzone_uid integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET "associated_inzone" = p_inzone_id,
        "associated_inzone_uid" = p_inzone_uid
    WHERE "char_id" = p_char_id;
$function$;

COMMIT;
