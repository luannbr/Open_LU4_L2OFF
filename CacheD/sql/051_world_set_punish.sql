-- Current Cached.exe primary opcode 48 (SetPunishPacket) persistence.
--
-- The current handler and DB helper fix four 32-bit inputs in this order:
-- character ID, punishment ID, punish-on discriminator and remaining value.
-- The substantive full-arity-compatible Classic 3.0, Ertheia and modified-287
-- donors have the same relation effects. The branch structure below keeps
-- their no-op cases instead of normalizing them into an upsert.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setpunish"(
    p_char_id integer,
    p_punish_id integer,
    p_punish_on integer,
    p_remain integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    matching_rows integer := 0;
BEGIN
    SELECT count(*)
    INTO matching_rows
    FROM "world"."user_punish" AS punishment
    WHERE punishment."char_id" = p_char_id
      AND punishment."punish_id" = p_punish_id;

    IF matching_rows > 0 AND p_remain = 0 THEN
        DELETE FROM "world"."user_punish" AS punishment
        WHERE punishment."char_id" = p_char_id
          AND punishment."punish_id" = p_punish_id;
    END IF;

    IF matching_rows > 0 AND p_remain > 0 THEN
        IF p_punish_on = 1 THEN
            UPDATE "world"."user_punish" AS punishment
            SET "remain_game" = p_remain
            WHERE punishment."char_id" = p_char_id
              AND punishment."punish_id" = p_punish_id;
        END IF;

        IF p_punish_on = 0 THEN
            UPDATE "world"."user_punish" AS punishment
            SET "remain_real" = p_remain
            WHERE punishment."char_id" = p_char_id
              AND punishment."punish_id" = p_punish_id;
        END IF;
    END IF;

    IF matching_rows = 0 AND p_remain > 0 THEN
        IF p_punish_on = 1 THEN
            INSERT INTO "world"."user_punish" (
                "char_id",
                "punish_id",
                "punish_on",
                "remain_game"
            ) VALUES (
                p_char_id,
                p_punish_id,
                1,
                p_remain
            );
        END IF;

        IF p_punish_on = 0 THEN
            INSERT INTO "world"."user_punish" (
                "char_id",
                "punish_id",
                "punish_on",
                "remain_real"
            ) VALUES (
                p_char_id,
                p_punish_id,
                0,
                p_remain
            );
        END IF;
    END IF;
END;
$function$;

COMMIT;
