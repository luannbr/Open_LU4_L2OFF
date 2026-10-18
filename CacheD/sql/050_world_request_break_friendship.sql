-- Current Cached.exe primary opcode 46 (RequestBreakFriendship) database
-- slice. Current code fixes two unsigned-integer-compatible inputs and one
-- integer result. The current-revision database families agree on deleting
-- both directed user_friend rows in one statement and returning 1 only when
-- exactly two rows were removed. A one-row inconsistent state is deliberately
-- left deleted while returning 0, as in the kept T-SQL behavior.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_breakfriendship"(
    p_char_id integer,
    p_friend_char_id integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    affected_rows integer := 0;
BEGIN
    DELETE FROM "world"."user_friend" AS friend
    WHERE
        (friend.char_id = p_char_id AND
         friend.friend_char_id = p_friend_char_id)
        OR
        (friend.char_id = p_friend_char_id AND
         friend.friend_char_id = p_char_id);

    GET DIAGNOSTICS affected_rows = ROW_COUNT;
    RETURN QUERY
    SELECT CASE WHEN affected_rows = 2 THEN 1 ELSE 0 END;
END;
$function$;

COMMIT;
