-- Current Cached.exe CMiniGameAgitDB extended handlers 47 through 52.
--
-- Current machine code fixes all eight callable ABIs and result-column
-- orders.  Classic 3.0, modified 287 and Ertheia have identical bodies,
-- including the legacy self-comparison in DeleteFromMinigameAgitPledge.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadminigameagitstatus"(
    p_agit_id integer
) RETURNS TABLE (
    elapsed_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT status."elapsed_time"
    FROM "world"."minigame_agit_status" AS status
    WHERE status."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadminigameagitpledge"(
    p_agit_id integer
) RETURNS TABLE (
    pledge_id integer,
    point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT pledge."pledge_id", pledge."point"
    FROM "world"."minigame_agit_pledge" AS pledge
    WHERE pledge."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadminigameagitmember"(
    p_agit_id integer
) RETURNS TABLE (
    pledge_id integer,
    char_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT member."pledge_id", member."char_id"
    FROM "world"."minigame_agit_member" AS member
    WHERE member."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setminigameagitstatus"(
    p_agit_id integer,
    p_elapsed_time integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM "world"."minigame_agit_status" AS status
        WHERE status."agit_id" = p_agit_id
    ) THEN
        INSERT INTO "world"."minigame_agit_status" (
            "agit_id",
            "elapsed_time"
        ) VALUES (
            p_agit_id,
            p_elapsed_time
        );
    ELSE
        UPDATE "world"."minigame_agit_status" AS status
        SET "elapsed_time" = p_elapsed_time,
            "propose_time" = "world"."mssql_getdate"()
        WHERE status."agit_id" = p_agit_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertintominigameagitpledge"(
    p_agit_id integer,
    p_pledge_id integer,
    p_point integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM "world"."minigame_agit_pledge" AS pledge
        WHERE pledge."agit_id" = p_agit_id
          AND pledge."pledge_id" = p_pledge_id
    ) THEN
        INSERT INTO "world"."minigame_agit_pledge" (
            "agit_id",
            "pledge_id",
            "point"
        ) VALUES (
            p_agit_id,
            p_pledge_id,
            p_point
        );
        RETURN QUERY SELECT 0;
    ELSE
        RETURN QUERY SELECT -1;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletefromminigameagitpledge"(
    p_agit_id integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- Shipping T-SQL says `pledge_id = pledge_id`U+003B all three compatible
    -- donors therefore ignore the second parameter and delete every pledge
    -- row for this agit.  Preserve that observable legacy behavior.
    DELETE FROM "world"."minigame_agit_pledge" AS pledge
    WHERE pledge."agit_id" = p_agit_id
      AND pledge."pledge_id" = pledge."pledge_id";
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertintominigameagitmember"(
    p_agit_id integer,
    p_pledge_id integer,
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."minigame_agit_member" (
        "agit_id",
        "pledge_id",
        "char_id"
    ) VALUES (
        p_agit_id,
        p_pledge_id,
        p_char_id
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetminigameagit"(
    p_agit_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."minigame_agit_status"
    WHERE "agit_id" = p_agit_id;
    DELETE FROM "world"."minigame_agit_pledge"
    WHERE "agit_id" = p_agit_id;
    DELETE FROM "world"."minigame_agit_member"
    WHERE "agit_id" = p_agit_id;
END;
$function$;

COMMIT;
