-- Current Cached.exe extended slots 371-380: beauty-purchase loading and
-- character potential/AP persistence. The manipulate/trade handlers use the
-- already recovered item transaction procedures and add no SQL surface.
--
-- Cached.exe fixes every callable ABI below. Classic 3.0, modified 287 and
-- Ertheia keep identical substantive bodies for all six procedures.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadbeautypurchaselist"(
    p_char_id integer
) RETURNS TABLE(
    "beauty_type" integer,
    "beauty_upper_id" integer,
    "beauty_id" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        purchase."beauty_type",
        purchase."beauty_upper_id",
        purchase."beauty_id"
    FROM "world"."user_beauty_purchase" AS purchase
    WHERE purchase."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatehairaccinfo"(
    p_char_id integer,
    p_hair_accessory_flag integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_beauty" AS beauty
    SET "hair_accessory_flag" = p_hair_accessory_flag
    WHERE beauty."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setacquirepotentialskill"(
    p_char_id integer,
    p_subjob integer,
    p_potential_type integer,
    p_skill_id integer,
    p_skill_level integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_potential_skill" AS skill
        WHERE skill."user_charid" = p_char_id
          AND skill."subjob" = p_subjob
          AND skill."potential_type" = p_potential_type
          AND skill."skill_id" = p_skill_id
    ) THEN
        UPDATE "world"."user_potential_skill" AS skill
        SET "skill_lev" = p_skill_level
        WHERE skill."user_charid" = p_char_id
          AND skill."subjob" = p_subjob
          AND skill."potential_type" = p_potential_type
          AND skill."skill_id" = p_skill_id;
    ELSE
        INSERT INTO "world"."user_potential_skill" (
            "user_charid",
            "subjob",
            "potential_type",
            "skill_id",
            "skill_lev"
        ) VALUES (
            p_char_id,
            p_subjob,
            p_potential_type,
            p_skill_id,
            p_skill_level
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setpotentialpoint"(
    p_char_id integer,
    p_subjob integer,
    p_type_count_1 integer,
    p_type_count_2 integer,
    p_type_count_3 integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_potential_point" AS point
        WHERE point."user_charid" = p_char_id
          AND point."subjob" = p_subjob
    ) THEN
        UPDATE "world"."user_potential_point" AS point
        SET
            "type1" = p_type_count_1,
            "type2" = p_type_count_2,
            "type3" = p_type_count_3
        WHERE point."user_charid" = p_char_id
          AND point."subjob" = p_subjob;
    ELSE
        INSERT INTO "world"."user_potential_point" (
            "user_charid",
            "subjob",
            "type1",
            "type2",
            "type3"
        ) VALUES (
            p_char_id,
            p_subjob,
            p_type_count_1,
            p_type_count_2,
            p_type_count_3
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetacquirepotentialskill"(
    p_char_id integer,
    p_subjob integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."user_potential_skill" AS skill
    WHERE skill."user_charid" = p_char_id
      AND skill."subjob" = p_subjob;

    UPDATE "world"."user_potential_point" AS point
    SET "type1" = 0, "type2" = 0, "type3" = 0
    WHERE point."user_charid" = p_char_id
      AND point."subjob" = p_subjob;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savecharacterap"(
    p_char_id integer,
    p_ability_points integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS character
    SET "pp" = p_ability_points
    WHERE character."char_id" = p_char_id
$function$;

COMMIT;
