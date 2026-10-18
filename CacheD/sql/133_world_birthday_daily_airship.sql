-- Extended requests 171-180. Current Cached.exe fixes the callable names,
-- parameter/result ABI and packet consumers. The relational bodies are shared
-- by the compatible Classic 3.0, 287, Ertheia and High Five database families.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getlastbirthdaydate"(
    p_char_id integer,
    p_event_type integer
) RETURNS TABLE (
    gift_year integer,
    gift_month integer,
    gift_day integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        COALESCE(EXTRACT(YEAR FROM birthday."get_date")::integer, 0),
        COALESCE(EXTRACT(MONTH FROM birthday."get_date")::integer, 0),
        COALESCE(EXTRACT(DAY FROM birthday."get_date")::integer, 0)
    FROM "world"."user_birthday_history" AS birthday
    WHERE birthday."char_id" = p_char_id
      AND birthday."event_id" = p_event_type;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setbirthday"(
    p_char_id integer,
    p_date text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS character
    SET "create_date" = p_date::timestamp without time zone
    WHERE character."char_id" = p_char_id;
$function$;

-- The current CUser::SetDailyQuest helper binds the target long value as
-- SQL_C_SLONG/SQL_DOUBLE. Keep that ABI at the PostgreSQL boundary and store
-- the donor table's 32-bit integer value.
CREATE OR REPLACE FUNCTION "world"."lin_setdailyquest"(
    p_char_id integer,
    p_quest_id integer,
    p_complete_date double precision
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_daily_quest" AS quest
    SET "complete_date" = p_complete_date::integer
    WHERE quest."char_id" = p_char_id
      AND quest."quest_id" = p_quest_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_daily_quest" (
            "char_id",
            "quest_id",
            "complete_date"
        ) VALUES (
            p_char_id,
            p_quest_id,
            p_complete_date::integer
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaddailyquest"(
    p_char_id integer
) RETURNS TABLE (
    quest_id integer,
    complete_date integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        COALESCE(quest."quest_id", 0),
        COALESCE(quest."complete_date", 0)
    FROM "world"."user_daily_quest" AS quest
    WHERE quest."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savedominionrenamed"(
    p_char_id integer,
    p_is_renamed integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."dominion_renamed" AS renamed
    SET "is_renamed" = p_is_renamed
    WHERE renamed."char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."dominion_renamed" (
            "char_id",
            "is_renamed"
        ) VALUES (
            p_char_id,
            p_is_renamed
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaddominionrenamed"(
    p_char_id integer
) RETURNS TABLE (is_renamed integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT renamed."is_renamed"
    FROM "world"."dominion_renamed" AS renamed
    WHERE renamed."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletealldominionrenamed"()
RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."dominion_renamed";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadairship"()
RETURNS TABLE (
    airship_id integer,
    airship_type integer,
    owner_id integer,
    airship_fuel integer,
    airship_typeid integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        airship."airship_id",
        airship."airship_type",
        airship."owner_id",
        airship."airship_fuel",
        airship."airship_typeid"
    FROM "world"."airship" AS airship
    WHERE airship."deleted" = 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createairship"(
    p_airship_type integer,
    p_owner_id integer,
    p_fuel integer,
    p_airship_typeid integer
) RETURNS TABLE (airship_id integer)
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."airship" AS created (
        "airship_type",
        "owner_id",
        "airship_fuel",
        "airship_typeid",
        "deleted"
    ) VALUES (
        p_airship_type,
        p_owner_id,
        p_fuel,
        p_airship_typeid,
        0
    )
    RETURNING created."airship_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveairship"(
    p_airship_id integer,
    p_fuel integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."airship" AS airship
    SET "airship_fuel" = p_fuel
    WHERE airship."airship_id" = p_airship_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_removeairship"(
    p_airship_id integer,
    p_reason integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."airship" AS airship
    SET "deleted" = p_reason
    WHERE airship."airship_id" = p_airship_id;
$function$;

COMMIT;
