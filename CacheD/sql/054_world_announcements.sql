-- Current Cached.exe login/interval announcement and petition-message ABI.
--
-- Current machine bindings fix all seven procedure signatures and result
-- orders. The relational bodies below are shared by every substantive
-- full-arity-compatible C1, Classic 3.0, Ertheia and modified-287 donor.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getloginannounce"()
RETURNS TABLE (
    announcement_id integer,
    announcement_message text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        announcement."announce_id",
        announcement."announce_msg"::text
    FROM "world"."login_announce" AS announcement
    WHERE announcement."interval_10" = 0
    ORDER BY announcement."announce_id" ASC
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setloginannounce"(
    p_announce_id integer,
    p_announce_message text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."login_announce" AS announcement
        WHERE announcement."announce_id" = p_announce_id
          AND announcement."interval_10" = 0
    ) THEN
        UPDATE "world"."login_announce" AS announcement
        SET "announce_msg" = p_announce_message
        WHERE announcement."announce_id" = p_announce_id
          AND announcement."interval_10" = 0;
    ELSE
        INSERT INTO "world"."login_announce" (
            "announce_id",
            "announce_msg",
            "interval_10"
        ) VALUES (
            p_announce_id,
            p_announce_message,
            0
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delloginannounce"(
    p_announce_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."login_announce" AS announcement
    WHERE announcement."announce_id" = p_announce_id
      AND announcement."interval_10" = 0
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getintervalannounce"()
RETURNS TABLE (
    interval_10 integer,
    announcement_id integer,
    announcement_message text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        announcement."interval_10",
        announcement."announce_id",
        announcement."announce_msg"::text
    FROM "world"."login_announce" AS announcement
    WHERE announcement."interval_10" > 0
    ORDER BY
        announcement."interval_10" ASC,
        announcement."announce_id" ASC
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setintervalannounce"(
    p_interval integer,
    p_announce_id integer,
    p_announce_message text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."login_announce" AS announcement
        WHERE announcement."interval_10" = p_interval
          AND announcement."announce_id" = p_announce_id
    ) THEN
        UPDATE "world"."login_announce" AS announcement
        SET "announce_msg" = p_announce_message
        WHERE announcement."interval_10" = p_interval
          AND announcement."announce_id" = p_announce_id;
    ELSE
        INSERT INTO "world"."login_announce" (
            "interval_10",
            "announce_id",
            "announce_msg"
        ) VALUES (
            p_interval,
            p_announce_id,
            p_announce_message
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delintervalannounce"(
    p_interval integer,
    p_announce_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."login_announce" AS announcement
    WHERE announcement."interval_10" = p_interval
      AND announcement."announce_id" = p_announce_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getpetitionmsg"(
    p_char_id integer
) RETURNS TABLE (
    petition_message text
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    RETURN QUERY
    SELECT COALESCE(petition."msg", '')::text
    FROM "world"."petitionmsg" AS petition
    WHERE petition."char_id" = p_char_id;

    DELETE FROM "world"."petitionmsg" AS petition
    WHERE petition."char_id" = p_char_id;
END;
$function$;

COMMIT;
