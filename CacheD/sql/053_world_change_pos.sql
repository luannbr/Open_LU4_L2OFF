-- Current Cached.exe primary opcode 50 (ChangePosPacket) persistence.
--
-- The current handler, CUser::ChangeLocationExtern and its database helper
-- fix one UTF-16 character name, three 32-bit coordinates and one integer
-- result. The old C1 world-ID input is absent from the current executable.
-- Compatible modern donors agree on updating all matching rows and returning
-- a positive character ID only when the update found a character.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_changecharacterlocation"(
    p_character_name text,
    p_x integer,
    p_y integer,
    p_z integer
) RETURNS TABLE (character_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    affected_rows integer := 0;
    selected_character_id integer := 0;
BEGIN
    UPDATE "world"."user_data" AS character
    SET "xloc" = p_x,
        "yloc" = p_y,
        "zloc" = p_z
    WHERE "world"."mssql_text_eq"(
        character."char_name",
        p_character_name
    );

    GET DIAGNOSTICS affected_rows = ROW_COUNT;
    IF affected_rows > 0 THEN
        SELECT character."char_id"
        INTO selected_character_id
        FROM "world"."user_data" AS character
        WHERE "world"."mssql_text_eq"(
            character."char_name",
            p_character_name
        )
        LIMIT 1;
    END IF;

    RETURN QUERY SELECT selected_character_id;
END;
$function$;

COMMIT;
