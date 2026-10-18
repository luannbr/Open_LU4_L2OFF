-- Current Cached.exe primary opcodes 143/144 database slice.
-- The current image fixes all four call ABIs. Classic 3.0, modified 287 and
-- Ertheia donors agree on the ID-based block-list bodies, unblock history,
-- point-type-6 initialization and accumulated-point update meaning.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_addblocklist"(
    p_char_id integer,
    p_block_char_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM "world"."user_blocklist"
        WHERE char_id = p_char_id
          AND block_char_id = p_block_char_id
    ) THEN
        INSERT INTO "world"."user_blocklist" (
            char_id,
            block_char_id,
            block_memo,
            date
        ) VALUES (
            p_char_id,
            p_block_char_id,
            '',
            "world"."mssql_getdate"()
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteblocklist"(
    p_char_id integer,
    p_block_char_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_blocklist"
        WHERE char_id = p_char_id
          AND block_char_id = p_block_char_id
    ) THEN
        IF EXISTS (
            SELECT 1
            FROM "world"."user_unblocklist"
            WHERE char_id = p_char_id
              AND block_char_id = p_block_char_id
        ) THEN
            UPDATE "world"."user_unblocklist"
            SET date = "world"."mssql_getdate"()
            WHERE char_id = p_char_id
              AND block_char_id = p_block_char_id;
        ELSE
            INSERT INTO "world"."user_unblocklist" (
                char_id,
                block_char_id,
                block_char_name,
                date
            ) VALUES (
                p_char_id,
                p_block_char_id,
                '',
                "world"."mssql_getdate"()
            );
        END IF;

        DELETE FROM "world"."user_blocklist"
        WHERE char_id = p_char_id
          AND block_char_id = p_block_char_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaduserpoint"(
    p_char_id integer,
    p_point_type integer
) RETURNS TABLE (point integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_point integer;
BEGIN
    SELECT user_point.point
    INTO v_point
    FROM "world"."user_point" AS user_point
    WHERE user_point.char_id = p_char_id
      AND user_point.point_type = p_point_type;

    IF v_point IS NULL AND p_point_type = 6 THEN
        INSERT INTO "world"."user_point" (
            char_id,
            point_type,
            point
        ) VALUES (
            p_char_id,
            p_point_type,
            20000
        );
        v_point := 20000;
    END IF;

    RETURN QUERY SELECT COALESCE(v_point, 0);
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveuserpoint"(
    p_char_id integer,
    p_point_type integer,
    p_point integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_point_now integer;
    v_accumulated_point bigint;
    v_row_count integer;
BEGIN
    SELECT user_point.point, user_point.accumulated_point
    INTO v_point_now, v_accumulated_point
    FROM "world"."user_point" AS user_point
    WHERE user_point.char_id = p_char_id
      AND user_point.point_type = p_point_type;

    v_point_now := COALESCE(v_point_now, 0);
    v_accumulated_point := COALESCE(v_accumulated_point, 0);
    IF v_point_now < p_point THEN
        v_accumulated_point :=
            v_accumulated_point + p_point - v_point_now;
    END IF;

    UPDATE "world"."user_point"
    SET point = p_point,
        accumulated_point = v_accumulated_point
    WHERE char_id = p_char_id
      AND point_type = p_point_type;
    GET DIAGNOSTICS v_row_count = ROW_COUNT;

    IF v_row_count = 0 THEN
        INSERT INTO "world"."user_point" (
            char_id,
            point_type,
            point,
            accumulated_point
        ) VALUES (
            p_char_id,
            p_point_type,
            p_point,
            v_accumulated_point
        );
    END IF;
END;
$function$;

COMMIT;
