-- Current Cached.exe primary opcode 69 (RequestPetiSaveMsg) persistence.
--
-- The current machine fixes the procedure ABI as (unsigned-compatible
-- character ID, UTF-16 message) and ignores the result. All compatible
-- substantive donor bodies update the existing character row or insert it.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setpetitionmsg"(
    p_char_id integer,
    p_message text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."petitionmsg" AS petition
        WHERE petition."char_id" = p_char_id
    ) THEN
        UPDATE "world"."petitionmsg" AS petition
        SET "msg" = p_message
        WHERE petition."char_id" = p_char_id;
    ELSE
        INSERT INTO "world"."petitionmsg" (
            "char_id",
            "msg"
        ) VALUES (
            p_char_id,
            p_message
        );
    END IF;
END;
$function$;

COMMIT;
