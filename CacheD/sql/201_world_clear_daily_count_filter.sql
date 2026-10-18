-- Source bug: the shipping lin_ClearDailyCount body is literally
-- `where @char_id = @char_id and @event_id = @event_id`, so each request
-- deleted every user_daily_count row instead of one character's event row.
-- Migration 144 kept that body. This migration filters on the columns.
-- The signature and the result column are unchanged.
-- See CACHED-DAILY-COUNT-001 in docs/SHIPPING_BUG_FIXES.md.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_cleardailycount"(
    p_char_id integer,
    p_event_id integer
) RETURNS TABLE (cleared_count integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_deleted_rows integer;
BEGIN
    -- Shipping donor text:
    -- `where @char_id = @char_id and @event_id = @event_id`.
    DELETE FROM "world"."user_daily_count"
    WHERE "char_id" = p_char_id
      AND "event_id" = p_event_id;

    GET DIAGNOSTICS v_deleted_rows = ROW_COUNT;
    RETURN QUERY SELECT v_deleted_rows;
END;
$function$;

COMMIT;
