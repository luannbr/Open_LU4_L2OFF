-- Current Cached.exe primary opcode 7 (SaveCharacterPacket) database slice.
--
-- CUser::Save already reaches lin_SaveCharacter from migration 023.  This
-- slice adds the three other current call ABIs used after that save.  The
-- user_log relation itself has one main owner in migration 001.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE INDEX IF NOT EXISTS "ix_user_log_character_transition"
    ON "world"."user_log" (
        "char_id",
        "log_id",
        "log_to",
        "log_date" DESC
    );

CREATE OR REPLACE FUNCTION "world"."lin_adduserlog"(
    p_char_id integer,
    p_log_id integer,
    p_log_from integer,
    p_log_to integer,
    p_use_time integer,
    p_subjob_id integer DEFAULT -1
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_subjob_id integer := p_subjob_id;
BEGIN
    IF v_subjob_id < 0 THEN
        SELECT character.subjob_id
        INTO v_subjob_id
        FROM "world"."user_data" AS character
        WHERE character.char_id = p_char_id;
    END IF;

    INSERT INTO "world"."user_log" (
        char_id,
        log_id,
        log_from,
        log_to,
        use_time,
        subjob_id
    ) VALUES (
        p_char_id,
        p_log_id::smallint,
        p_log_from,
        p_log_to,
        p_use_time,
        v_subjob_id
    );
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getuserlogtime"(
    p_char_id integer,
    p_log_id integer,
    p_log_to integer
) RETURNS TABLE (use_time integer)
LANGUAGE sql
STABLE
AS $function$
    -- Every compatible SQL family keeps `IF @use_time = NULL`, which is
    -- never true under its ANSI_NULLS setting.  So the observable
    -- current-client contract is one nullable latest-log value, not the
    -- intended user_data fallback.  CUser initializes its bound value to 0.
    SELECT (
        SELECT transition.use_time
        FROM "world"."user_log" AS transition
        WHERE transition.char_id = p_char_id
          AND transition.log_id = p_log_id
          AND transition.log_to = p_log_to
        ORDER BY transition.log_date DESC
        LIMIT 1
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_reloaduserhighestexpandlevel"(
    p_char_id integer
) RETURNS TABLE (
    highest_exp bigint,
    highest_level integer
)
LANGUAGE sql
STABLE
AS $function$
    WITH top_subjob AS (
        SELECT subjob.subjob_id,
               subjob.exp,
               subjob.level::integer AS level
        FROM "world"."user_subjob" AS subjob
        WHERE p_char_id > 0
          AND subjob.char_id = p_char_id
        -- SQL Server puts NULL below non-NULL values for DESC ordering.
        ORDER BY subjob.exp DESC NULLS LAST
        LIMIT 1
    )
    SELECT
        CASE
            WHEN subjob.subjob_id IS NULL THEN character.exp
            WHEN character.subjob_id = subjob.subjob_id
                THEN character.exp
            WHEN character.exp >= subjob.exp THEN character.exp
            ELSE subjob.exp
        END::bigint,
        CASE
            WHEN subjob.subjob_id IS NULL THEN character.lev
            WHEN character.subjob_id = subjob.subjob_id
                THEN character.lev
            WHEN character.exp >= subjob.exp THEN character.lev
            ELSE subjob.level
        END::integer
    FROM "world"."user_data" AS character
    LEFT JOIN top_subjob AS subjob ON true
    WHERE p_char_id > 0
      AND character.char_id = p_char_id;
$function$;

COMMIT;
