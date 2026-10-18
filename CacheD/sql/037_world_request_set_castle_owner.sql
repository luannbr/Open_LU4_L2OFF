-- Current Cached.exe primary opcode 25 (RequestSetCastleOwner) persistence.
--
-- CSocketHandler::OnRequestSetCastleOwner 0x005979d0 and the exact binding
-- adapter at 0x0040a708 establish the ordered four-int ABI. Three modern,
-- full-arity-compatible donor families have identical normalized bodies and
-- relational effects. Older two-input C1 definitions are not
-- used for this current contract.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setcastleowner2"(
    p_world_id integer,
    p_pledge_id integer,
    p_castle_id integer,
    p_castle_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."castle"
    SET "pledge_id" = p_pledge_id,
        "world_id" = p_world_id,
        "castle_type" = p_castle_type
    WHERE "id" = p_castle_id;
$function$;

COMMIT;
