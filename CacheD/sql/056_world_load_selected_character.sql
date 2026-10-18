-- Current Cached.exe primary opcode 55 (LoadSelectedCharacter) dependencies.
--
-- The current machine fixes the ordered result widths and procedure arities:
-- lin_LoadSociality returns int32, int32, UTF-16 timestamp.
-- lin_ResetSociality accepts int32, int32, int32, UTF-16 timestamp. And
-- lin_LoadTrainingZonePos returns three int32 coordinates. Classic 3.0 and
-- the 287/Ertheia exports agree on the table and column meaning below.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadsociality"(
    p_char_id integer
) RETURNS TABLE (
    sociality integer,
    used_sulffrage integer,
    last_changed varchar(19)
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_sociality" AS value
        WHERE value."char_id" = p_char_id
    ) THEN
        RETURN QUERY
        SELECT value."sociality",
               value."used_sulffrage",
               to_char(
                   value."last_changed",
                   'YYYY-MM-DD HH24:MI:SS')::varchar(19)
        FROM "world"."user_sociality" AS value
        WHERE value."char_id" = p_char_id;
    ELSE
        RETURN QUERY
        SELECT 0, 0, '0000-00-00 00:00:00'::varchar(19);
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetsociality"(
    p_char_id integer,
    p_sociality integer,
    p_sulffrage integer,
    p_last_changed text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_sociality" (
        "char_id",
        "sociality",
        "used_sulffrage",
        "last_changed"
    ) VALUES (
        p_char_id,
        p_sociality,
        p_sulffrage,
        p_last_changed::timestamp without time zone
    )
    ON CONFLICT ("char_id") DO UPDATE
    SET "sociality" = EXCLUDED."sociality",
        "used_sulffrage" = EXCLUDED."used_sulffrage",
        "last_changed" = EXCLUDED."last_changed";
$function$;

-- Current CUser::SetSociality 0x005aea14 binds exactly these two integers.
-- The compatible Classic 3.0/287 body updates an existing sociality row and
-- does not insert one when it is absent.
CREATE OR REPLACE FUNCTION "world"."lin_updatesociality"(
    p_char_id integer,
    p_sociality integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_sociality"
    SET "sociality" = p_sociality
    WHERE "char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadtrainingzonepos"(
    p_char_id integer
) RETURNS TABLE (
    xloc integer,
    yloc integer,
    zloc integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT training."xloc",
           training."yloc",
           training."zloc"
    FROM "world"."trainingzone_state" AS training
    WHERE training."char_id" = p_char_id
      AND training."states" = 1;
$function$;

COMMIT;
