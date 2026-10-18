-- Current Cached.exe CUser::AddSubJobHenna 0x005af12c and
-- CUser::DelSubJobHenna 0x005af360, reached by administrative AQ
-- 71/72/74/79/80/95 through the original CUser Henna dispatchers.
--
-- The image fixes the three integer inputs as character ID, subjob ID and
-- Henna ID. All six substantive signature-compatible donor families agree
-- on the ordered first-empty / first-match update bodies below.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_addsubjobhenna"(
    p_char_id integer,
    p_subjob_id integer,
    p_henna integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_henna_1 integer := 0;
    v_henna_2 integer := 0;
    v_henna_3 integer := 0;
BEGIN
    SELECT COALESCE("henna_1", 0),
           COALESCE("henna_2", 0),
           COALESCE("henna_3", 0)
    INTO v_henna_1, v_henna_2, v_henna_3
    FROM "world"."user_subjob"
    WHERE "char_id" = p_char_id
      AND "subjob_id" = p_subjob_id;

    IF v_henna_1 = 0 THEN
        UPDATE "world"."user_subjob"
        SET "henna_1" = p_henna
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;
    ELSIF v_henna_2 = 0 THEN
        UPDATE "world"."user_subjob"
        SET "henna_2" = p_henna
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;
    ELSIF v_henna_3 = 0 THEN
        UPDATE "world"."user_subjob"
        SET "henna_3" = p_henna
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delsubjobhenna"(
    p_char_id integer,
    p_subjob_id integer,
    p_henna integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_henna_1 integer := 0;
    v_henna_2 integer := 0;
    v_henna_3 integer := 0;
BEGIN
    SELECT COALESCE("henna_1", 0),
           COALESCE("henna_2", 0),
           COALESCE("henna_3", 0)
    INTO v_henna_1, v_henna_2, v_henna_3
    FROM "world"."user_subjob"
    WHERE "char_id" = p_char_id
      AND "subjob_id" = p_subjob_id;

    IF p_henna = v_henna_1 THEN
        UPDATE "world"."user_subjob"
        SET "henna_1" = 0
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;
    ELSIF p_henna = v_henna_2 THEN
        UPDATE "world"."user_subjob"
        SET "henna_2" = 0
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;
    ELSIF p_henna = v_henna_3 THEN
        UPDATE "world"."user_subjob"
        SET "henna_3" = 0
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;
    END IF;
END;
$function$;

COMMIT;
