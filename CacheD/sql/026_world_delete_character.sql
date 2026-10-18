-- Current Cached.exe primary opcode 9 (DeleteCharacterPacket) database slice.
--
-- All bodies below are identical in the compatible classic-3.0, modified-287
-- and Ertheia donor families.  Parameter/result widths and the unusual caller
-- failure branches come from the current CacheD bind sites.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_regtotalcountcommission"(
    p_char_id integer
) RETURNS TABLE (registered_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."commission" AS commission
    WHERE commission.char_id = p_char_id
      AND commission.buyer_id = 0
      AND commission.item_delete_time IS NULL;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadsendpostbysenderid"(
    p_sender_id integer
) RETURNS TABLE (item_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."post_item_admin" AS item
    WHERE item.post_id IN (
        SELECT post.post_id
        FROM "world"."post" AS post
        WHERE post.sender_id = p_sender_id
          AND post.sender_type = 0
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_ismentee"(
    p_char_id integer
) RETURNS TABLE (mentor_id integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT mentor.mentor_id
    FROM "world"."user_mentor" AS mentor
    JOIN "world"."user_data" AS character
      ON character.char_id = mentor.mentor_id
    WHERE mentor.mentee_id = p_char_id
      AND character.account_id > 0
      AND mentor.status = 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_ismentor"(
    p_char_id integer
) RETURNS TABLE (mentee_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(mentor.mentee_id)::integer
    FROM "world"."user_mentor" AS mentor
    JOIN "world"."user_data" AS character
      ON character.char_id = mentor.mentee_id
    WHERE mentor.mentor_id = p_char_id
      AND character.account_id > 0
      AND mentor.status = 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepledgewaitinguser"(
    p_world_id integer,
    p_char_id integer
) RETURNS TABLE (pledge_id integer)
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."pledge_recruit_waiting_list" AS waiting
    WHERE waiting.char_id = p_char_id
      AND waiting.world_id = p_world_id
    RETURNING waiting.pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepledgedraftuser"(
    p_world_id integer,
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."pledge_draft_user_list" AS draft
    WHERE draft.char_id = p_char_id
      AND draft.world_id = p_world_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setcharacterdelete"(
    p_char_id integer
) RETURNS TABLE (
    delete_year integer,
    delete_month integer,
    delete_day integer,
    delete_hour integer,
    delete_minute integer,
    delete_second integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    -- SQL Server first converts GETDATE() to SMALLDATETIME: seconds are
    -- rounded to the nearest minute and the stored seconds component is zero.
    v_deleted_date timestamp without time zone :=
        "world"."mssql_getsmalldatetime"();
    v_rows integer := 0;
BEGIN
    UPDATE "world"."user_data" AS character
    SET temp_delete_date = v_deleted_date
    WHERE character.char_id = p_char_id;
    GET DIAGNOSTICS v_rows = ROW_COUNT;

    IF v_rows > 0 THEN
        RETURN QUERY SELECT
            extract(year FROM v_deleted_date)::integer,
            extract(month FROM v_deleted_date)::integer,
            extract(day FROM v_deleted_date)::integer,
            extract(hour FROM v_deleted_date)::integer,
            extract(minute FROM v_deleted_date)::integer,
            extract(second FROM v_deleted_date)::integer;
    ELSE
        RETURN QUERY SELECT 0, 0, 0, 0, 0, 0;
    END IF;
END;
$function$;

COMMIT;
