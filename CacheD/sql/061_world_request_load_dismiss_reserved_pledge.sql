-- Current Cached.exe primary opcode 66
-- (RequestLoadDismissReservedPledge) persistence.
--
-- The current machine code fixes the ABI as one signed integer input and two
-- unsigned 32-bit-compatible result columns: pledge_id and
-- dismiss_reserved_time. All substantive one-parameter donors select these
-- columns from pledge by status. The handler supplies status 3.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loaddismissreservedpledge"(
    p_status integer
) RETURNS TABLE (
    pledge_id integer,
    dismiss_reserved_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        pledge."pledge_id",
        pledge."dismiss_reserved_time"
    FROM "world"."pledge" AS pledge
    WHERE pledge."status" = p_status;
$function$;

COMMIT;
