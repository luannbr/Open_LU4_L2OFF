-- Current Cached.exe primary opcodes 203-204 and their CUser owners.
--
-- The current machine code fixes both call names and parameter order.  Every
-- substantive signature-compatible donor family has an identical normalized
-- body and relation effect for these two persistence operations.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_savehenna"(
    p_char_id integer,
    p_henna_1 integer,
    p_henna_2 integer,
    p_henna_3 integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_henna"
        WHERE "user_henna"."char_id" = p_char_id
    ) THEN
        UPDATE "world"."user_henna"
        SET "henna_1" = p_henna_1,
            "henna_2" = p_henna_2,
            "henna_3" = p_henna_3
        WHERE "user_henna"."char_id" = p_char_id;
    ELSE
        INSERT INTO "world"."user_henna" (
            "char_id",
            "henna_1",
            "henna_2",
            "henna_3"
        ) VALUES (
            p_char_id,
            p_henna_1,
            p_henna_2,
            p_henna_3
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savehennapremium"(
    p_char_id integer,
    p_henna_id integer,
    p_start_time integer,
    p_period integer,
    p_subjob_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_henna_premium"
    SET "henna_id" = p_henna_id,
        "start_time" = p_start_time,
        "period" = p_period
    WHERE "user_henna_premium"."char_id" = p_char_id
      AND "user_henna_premium"."subjob_id" = p_subjob_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_henna_premium" (
            "char_id",
            "henna_id",
            "start_time",
            "period",
            "subjob_id"
        ) VALUES (
            p_char_id,
            p_henna_id,
            p_start_time,
            p_period,
            p_subjob_id
        );
    END IF;
END;
$function$;

COMMIT;
